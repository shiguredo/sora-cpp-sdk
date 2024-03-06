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

import android.annotation.SuppressLint;
import android.content.Context;
import android.util.Log;

import java.util.ArrayList;
import java.util.HashSet;
import java.util.List;
import java.util.Set;

class SoraAudioManagerLegacy extends SoraAudioManagerBase {
    private static final String TAG = "SoraAudioManagerLegacy";
    private enum AudioDevice { SPEAKER_PHONE, WIRED_HEADSET, EARPIECE, BLUETOOTH, NONE }
    private final AudioDevice defaultAudioDevice;
    private final SoraBluetoothManager bluetoothManager;
    private Set<AudioDevice> audioDevices = new HashSet<>();
    private boolean running;
    private boolean savedIsSpeakerPhoneOn;
    private AudioDevice selectedAudioDevice;
    private AudioDevice lastConnectedAudioDevice;
    private boolean willOffHandsfree;

    static SoraAudioManagerLegacy create(Context context) {
        return new SoraAudioManagerLegacy(context);
    }

    private SoraAudioManagerLegacy(Context context) {
        super(context);

        bluetoothManager = SoraBluetoothManager.create(context, this, audioManager);

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
     */
    @Override
    public void start(OnChangeRouteObserver observer) {
        Log.d(TAG, "start");
        SoraThreadUtils.checkIsOnMainThread();
        if (running) {
            Log.e(TAG, "SoraAudioManager is already active");
            return;
        }
        running = true;

        // コールバックを設定する
        onChangeRouteObserver = observer;

        // 停止時に設定に戻すため設定を保存する
        savedIsSpeakerPhoneOn = audioManager.isSpeakerphoneOn();
        super.start(observer);

        // 初期化を行う
        selectedAudioDevice = AudioDevice.NONE;
        audioDevices.clear();

        // bluetooth の制御を開始する
        bluetoothManager.start();

        // 初期化を行った状態でデバイスの設定を行う
        // TODO(tnoho) この時点で初期値を指定しておきたいニーズが存在する可能性
        updateAudioDeviceState();

        registerWiredHeadsetReceiver();
    }

    // オーディオの制御を終了する
    @Override
    @SuppressLint("WrongConstant")
    public void stop() {
        Log.d(TAG, "stop");
        SoraThreadUtils.checkIsOnMainThread();
        if (!running) {
            Log.e(TAG, "Trying to stop SoraAudioManager in not running");
            return;
        }
        running = false;

        // 有線ヘッドセットの接続を通知するレシーバーを解除
        unregisterWiredHeadsetReceiver();

        // bluetooth の制御を終了する
        bluetoothManager.stop();

        // 開始時に保存していた設定に戻す
        setSpeakerphoneOn(savedIsSpeakerPhoneOn);
        super.stop();
    }

    // ハンズフリーかを確認する
    @Override
    public boolean isHandsfree() {
        // audioManager.isSpeakerphoneOn() は意図しない結果を返すのでこうした
        return selectedAudioDevice == AudioDevice.SPEAKER_PHONE;
    }

    // ハンズフリーの設定を変更する
    private void setSpeakerphoneOn(boolean on) {
        boolean wasOn = audioManager.isSpeakerphoneOn();
        if (wasOn == on) {
            return;
        }
        audioManager.setSpeakerphoneOn(on);
    }

    // 状態に基づいてデバイスを選択する
    @Override
    void updateAudioDeviceState() {
        SoraThreadUtils.checkIsOnMainThread();
        if (!running) {
            return;
        }

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
        Log.d(TAG, "Update device state: "
                + "wired headset=" + hasWiredHeadset + ", "
                + "BT state=" + bluetoothManager.getState() + ", "
                + "need BT audio start=" + needBluetoothAudioStart);

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
        if (newAudioDevice != selectedAudioDevice) {
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
