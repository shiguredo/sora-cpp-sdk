/*
 *  Copyright 2014 The WebRTC Project Authors. All rights reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 *
 *  Modifications made by tnoho in 2024.
 */

package jp.shiguredo.sora.audiomanager;

import android.Manifest;
import android.annotation.SuppressLint;
import android.app.Activity;
import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
import android.content.IntentFilter;
import android.content.pm.PackageManager;
import android.media.AudioAttributes;
import android.media.AudioFocusRequest;
import android.media.AudioManager;
import android.os.Build;
import android.os.Handler;
import android.os.Looper;
import android.util.Log;

import androidx.annotation.Nullable;
import androidx.annotation.RequiresApi;

import java.util.ArrayList;
import java.util.HashSet;
import java.util.List;
import java.util.Set;

public class SoraAudioManager {
    private static final String TAG = "SoraAudioManager";

    public enum AudioDevice { SPEAKER_PHONE, WIRED_HEADSET, EARPIECE, BLUETOOTH, NONE }

    public interface OnChangeRouteObserver {
        // オーディオデバイスの変更を通知するコールバック
        void OnChangeRoute();
    }

    private final Context context;
    private final AudioManager audioManager;
    private final SoraBluetoothManager bluetoothManager;
    private Set<AudioDevice> audioDevices = new HashSet<>();
    private final BroadcastReceiver wiredHeadsetReceiver;
    private int savedAudioMode = AudioManager.MODE_INVALID;
    private boolean savedIsSpeakerPhoneOn;
    private boolean savedIsMicrophoneMute;
    private boolean hasWiredHeadset;
    private final AudioDevice defaultAudioDevice;
    private AudioDevice selectedAudioDevice;
    private AudioDevice lastConnectedAudioDevice;
    private boolean isSetHandsfree;
    private boolean willOffHandsfree;
    private Object audioFocus;
    @Nullable
    private OnChangeRouteObserver onChangeRouteObserver;

    // 有線ヘッドセットの接続を通知するレシーバー
    private class WiredHeadsetReceiver extends BroadcastReceiver {
        private static final int STATE_UNPLUGGED = 0;
        private static final int STATE_PLUGGED = 1;
        @Override
        public void onReceive(Context context, Intent intent) {
            int state = intent.getIntExtra("state", STATE_UNPLUGGED);
            hasWiredHeadset = (state == STATE_PLUGGED);
            updateAudioDeviceState();
        }
    }

    public static void requestPermissions(Activity activity) {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
            // 承認が必要なのは API レベル 31 からなのでこの分岐で良い
            if (activity.checkSelfPermission(Manifest.permission.BLUETOOTH_CONNECT)
                    != PackageManager.PERMISSION_GRANTED) {

                activity.requestPermissions(new String[]{Manifest.permission.BLUETOOTH_CONNECT}, 1);
            }
        }
    }

    public static SoraAudioManager create(Context context) {
        return new SoraAudioManager(context);
    }

    private SoraAudioManager(Context context) {
        SoraThreadUtils.checkIsOnMainThread();
        this.context = context;
        audioManager = (AudioManager) context.getSystemService(Context.AUDIO_SERVICE);
        bluetoothManager = SoraBluetoothManager.create(context, this, audioManager);
        wiredHeadsetReceiver = new WiredHeadsetReceiver();

        // デフォルトのデバイスを設定する
        // 受話用のスピーカーがある場合は受話用のスピーカーを使う
        if (hasEarpiece()) {
            defaultAudioDevice = AudioDevice.EARPIECE;
        } else {
            defaultAudioDevice = AudioDevice.SPEAKER_PHONE;
        }
    }

    /*
     * オーディオの制御を開始する
     * Java は destructor がないので start - stop にする
     * TODO(tnoho) 以下のパラメーターは start の段階で調整できてもいい気がする
     * - オーディオフォーカス
     * - モード
     * - マイクミュート
     */
    public void start(OnChangeRouteObserver observer) {
        SoraThreadUtils.checkIsOnMainThread();
        // コールバックを設定する
        onChangeRouteObserver = observer;

        // 停止時に設定に戻すため設定を保存する
        savedAudioMode = audioManager.getMode();
        savedIsSpeakerPhoneOn = audioManager.isSpeakerphoneOn();
        savedIsMicrophoneMute = audioManager.isMicrophoneMute();

        // オーディオフォーカスを取得する
        // 前のオーディオフォーカス保持者に再生の一時停止を期待する
        int result;
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
            audioFocus = new AudioFocusRequest.Builder(AudioManager.AUDIOFOCUS_GAIN_TRANSIENT)
                    .setAudioAttributes(
                            new AudioAttributes.Builder()
                                    .setUsage(AudioAttributes.USAGE_VOICE_COMMUNICATION)
                                    .setContentType(AudioAttributes.CONTENT_TYPE_SPEECH)
                                    .build()
                    ).build();
            result = audioManager.requestAudioFocus((AudioFocusRequest) audioFocus);
        } else {
            audioFocus = (AudioManager.OnAudioFocusChangeListener) focusChange -> {
                // 取ったからといってその後何かするわけではないのでログだけ出す
                Log.d(TAG, "onAudioFocusChange: " + focusChange);
            };
            result = audioManager.requestAudioFocus((AudioManager.OnAudioFocusChangeListener) audioFocus,
                    AudioManager.STREAM_VOICE_CALL, AudioManager.AUDIOFOCUS_GAIN_TRANSIENT);
        }
        if (result == AudioManager.AUDIOFOCUS_REQUEST_GRANTED) {
            Log.d(TAG, "Audio focus request granted for VOICE_CALL streams");
        } else {
            Log.e(TAG, "Audio focus request failed");
        }

        // VoIP 向けのモードに切り替え
        audioManager.setMode(AudioManager.MODE_IN_COMMUNICATION);

        // マイクのミュートは解除する
        setMicrophoneMute(false);

        // 初期化を行う
        selectedAudioDevice = AudioDevice.NONE;
        audioDevices.clear();

        // bluetooth の制御を開始する
        bluetoothManager.start();

        // 初期化を行った状態でデバイスの設定を行う
        // TODO(tnoho) この時点で初期値を指定しておきたいニーズが存在する可能性
        updateAudioDeviceState();

        // 有線ヘッドセットの接続を通知するレシーバーを登録する
        context.registerReceiver(
                wiredHeadsetReceiver,
                new IntentFilter(Intent.ACTION_HEADSET_PLUG));
    }

    // オーディオの制御を終了する
    @SuppressLint("WrongConstant")
    public void stop() {
        SoraThreadUtils.checkIsOnMainThread();

        // 有線ヘッドセットの接続を通知するレシーバーを解除
        context.unregisterReceiver(wiredHeadsetReceiver);

        // bluetooth の制御を終了する
        bluetoothManager.stop();

        // 開始時に保存していた設定に戻す
        setSpeakerphoneOn(savedIsSpeakerPhoneOn);
        setMicrophoneMute(savedIsMicrophoneMute);
        audioManager.setMode(savedAudioMode);

        // オーディオフォーカスを放棄する
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
            audioManager.abandonAudioFocusRequest((AudioFocusRequest) audioFocus);
        } else {
            audioManager.abandonAudioFocus((AudioManager.OnAudioFocusChangeListener) audioFocus);
        }
        audioFocus = null;

        // コールバックを破棄する
        onChangeRouteObserver = null;
    }

    // ハンズフリーかを確認する
    public boolean isHandsfree() {
        return audioManager.isSpeakerphoneOn();
    }

    // ハンズフリーに設定する
    public void setHandsfree(boolean on) {
        SoraThreadUtils.checkIsOnMainThread();
        if (isSetHandsfree == on) {
            return;
        }
        isSetHandsfree = on;
        updateAudioDeviceState();
    }

    // ハンズフリーの設定を変更する
    private void setSpeakerphoneOn(boolean on) {
        boolean wasOn = audioManager.isSpeakerphoneOn();
        if (wasOn == on) {
            return;
        }
        audioManager.setSpeakerphoneOn(on);
    }

    // マイクミュートの設定を変更する
    private void setMicrophoneMute(boolean on) {
        boolean wasMuted = audioManager.isMicrophoneMute();
        if (wasMuted == on) {
            return;
        }
        audioManager.setMicrophoneMute(on);
    }

    // 電話用スピーカーがデバイスにあるかを確認する
    private boolean hasEarpiece() {
        return context.getPackageManager().hasSystemFeature(PackageManager.FEATURE_TELEPHONY);
    }

    // 状態に基づいてデバイスを選択する
    public void updateAudioDeviceState() {
        SoraThreadUtils.checkIsOnMainThread();

        if (bluetoothManager.getState() == SoraBluetoothManager.State.HEADSET_AVAILABLE
                || bluetoothManager.getState() == SoraBluetoothManager.State.HEADSET_UNAVAILABLE
                || bluetoothManager.getState() == SoraBluetoothManager.State.SCO_DISCONNECTING) {
            // Bluetooth のデバイスを更新する
            bluetoothManager.updateDevice();
        }

        // 存在するオーディオデバイスのリストを生成する
        Set<AudioDevice> newAudioDevices = new HashSet<>();

        if (bluetoothManager.getState() == SoraBluetoothManager.State.SCO_CONNECTED
                || bluetoothManager.getState() == SoraBluetoothManager.State.SCO_CONNECTING
                || bluetoothManager.getState() == SoraBluetoothManager.State.HEADSET_AVAILABLE) {
            // Bluetooth デバイスが存在する
            newAudioDevices.add(AudioDevice.BLUETOOTH);
        }

        if (hasWiredHeadset) {
            // 有線ヘッドセットが接続されている
            newAudioDevices.add(AudioDevice.WIRED_HEADSET);
        }

        if (hasEarpiece()) {
            // 受話用スピーカーが存在する
            newAudioDevices.add(AudioDevice.EARPIECE);
        }

        // スピーカーは必ずある
        newAudioDevices.add(AudioDevice.SPEAKER_PHONE);

        // 利用可能なデバイス一覧に変更があったかを調べる
        boolean audioDeviceSetUpdated = !audioDevices.equals(newAudioDevices);

        if (audioDeviceSetUpdated) {
            // 変更があった場合には新規で追加されたデバイスを抜き出す
            List<AudioDevice> addedDevices = new ArrayList<>(newAudioDevices);
            addedDevices.removeAll(audioDevices);

            if (addedDevices.contains(AudioDevice.WIRED_HEADSET)) {
                // 有線ヘッドセットが新たに接続された
                lastConnectedAudioDevice = AudioDevice.WIRED_HEADSET;
                // 新規デバイスが接続された場合はハンズフリーは解除する
                isSetHandsfree = false;
            } else if (addedDevices.contains(AudioDevice.BLUETOOTH)) {
                // Bluetooth ヘッドセットが新たに接続された
                lastConnectedAudioDevice = AudioDevice.BLUETOOTH;
                if (isSetHandsfree) {
                    // 新規デバイスが接続された場合はハンズフリーは解除する
                    isSetHandsfree = false;
                    // SCO_CONNECTED まではハンズフリーの解除は待つ
                    willOffHandsfree = true;
                }
            }
        }

        // 利用可能なデバイス一覧を更新する
        audioDevices = newAudioDevices;

        // Bluetooth audio を開始する必要があるか
        boolean needBluetoothAudioStart =
                bluetoothManager.getState() == SoraBluetoothManager.State.HEADSET_AVAILABLE
                        && !isSetHandsfree
                        && (lastConnectedAudioDevice == AudioDevice.BLUETOOTH || !hasWiredHeadset);
        Log.d(TAG, "--- updateAudioDeviceState: "
                + "wired headset=" + hasWiredHeadset + ", "
                + "BT state=" + bluetoothManager.getState() + ", "
                + "needBluetoothAudioStart=" + needBluetoothAudioStart);

        // Bluetooth audio を停止する必要があるか
        boolean needBluetoothAudioStop =
                (bluetoothManager.getState() == SoraBluetoothManager.State.SCO_CONNECTED
                        || bluetoothManager.getState() == SoraBluetoothManager.State.SCO_CONNECTING)
                        && (isSetHandsfree
                        || (lastConnectedAudioDevice == AudioDevice.WIRED_HEADSET && hasWiredHeadset));

        if (needBluetoothAudioStop) {
            bluetoothManager.stopScoAudio();
            bluetoothManager.updateDevice();
        }

        if (needBluetoothAudioStart && !needBluetoothAudioStop) {
            // Bluetooth SCO audio を介しする
            if (!bluetoothManager.startScoAudio()) {
                // 失敗した場合はリストから BLUETOOTH を削除する
                audioDevices.remove(AudioDevice.BLUETOOTH);
                audioDeviceSetUpdated = true;
                willOffHandsfree = false;
            }
        }

        // ハンズフリーの解除を待つのは SCO_CONNECTING の間だけ
        willOffHandsfree = willOffHandsfree
                && bluetoothManager.getState() == SoraBluetoothManager.State.SCO_CONNECTING;

        final AudioDevice newAudioDevice;
        if (bluetoothManager.getState() == SoraBluetoothManager.State.SCO_CONNECTED) {
            newAudioDevice = AudioDevice.BLUETOOTH;
            willOffHandsfree = false;
        } else if (!isSetHandsfree && hasWiredHeadset) {
            newAudioDevice = AudioDevice.WIRED_HEADSET;
        } else if (isSetHandsfree || willOffHandsfree) {
            newAudioDevice = AudioDevice.SPEAKER_PHONE;
        } else {
            newAudioDevice = defaultAudioDevice;
        }
        if (newAudioDevice != selectedAudioDevice || audioDeviceSetUpdated) {
            Log.d(TAG, "New device status: "
                    + "available=" + audioDevices + ", "
                    + "selected=" + newAudioDevice);
            setSpeakerphoneOn(newAudioDevice == AudioDevice.SPEAKER_PHONE);
            selectedAudioDevice = newAudioDevice;
            if (onChangeRouteObserver != null) {
                onChangeRouteObserver.OnChangeRoute();
            }
        }
    }
}
