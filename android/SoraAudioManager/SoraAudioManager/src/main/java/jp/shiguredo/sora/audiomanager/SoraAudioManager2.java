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
import android.annotation.TargetApi;
import android.bluetooth.BluetoothHeadset;
import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
import android.content.IntentFilter;
import android.media.AudioDeviceInfo;
import android.os.Build;
import android.util.Log;

import java.util.ArrayList;
import java.util.List;

@TargetApi(Build.VERSION_CODES.S)
public class SoraAudioManager2 extends SoraAudioManager {
    private static final String TAG = "SoraAudioManager2";
    private final BroadcastReceiver bluetoothHeadsetReceiver;
    private final List<AudioDeviceInfo> audioDevices = new ArrayList<>();
    private AudioDeviceInfo selectedAudioDevice;
    private AudioDeviceInfo lastConnectedAudioDevice;

    private class BluetoothHeadsetBroadcastReceiver extends BroadcastReceiver {
        @Override
        public void onReceive(Context context, Intent intent) {
            if (!running) {
                return;
            }
            final String action = intent.getAction();
            if (action == null) {
                return;
            }
            /*
             * ヘッドセットプロファイルの状態変化
             * 他のオーディオデバイスを使用中に Bluetooth をオンにすると発火します
             */
            Log.d(TAG, "BluetoothHeadsetBroadcastReceiver.onReceive: "
                    + "a=" + action);
            if (action.equals(BluetoothHeadset.ACTION_CONNECTION_STATE_CHANGED)) {
                final int state =
                        intent.getIntExtra(BluetoothHeadset.EXTRA_STATE, BluetoothHeadset.STATE_DISCONNECTED);
                Log.d(TAG, "BluetoothHeadsetBroadcastReceiver.onReceive: "
                        + "a=ACTION_CONNECTION_STATE_CHANGED, "
                        + "s=" + SoraBluetoothManager.stateToString(state) + ", "
                        + "sb=" + isInitialStickyBroadcast());
                if (state == BluetoothHeadset.STATE_CONNECTED) {
                    // Bluetooth ヘッドセットとが接続された
                    updateAudioDeviceState();
                } else if (state == BluetoothHeadset.STATE_DISCONNECTED) {
                    // おそらく Bluetooth が通話中に切られた
                    updateAudioDeviceState();
                }
            } else if (action.equals(BluetoothHeadset.ACTION_AUDIO_STATE_CHANGED)) {
                /* SCO の状態変化 */
                final int state = intent.getIntExtra(
                        BluetoothHeadset.EXTRA_STATE, BluetoothHeadset.STATE_AUDIO_DISCONNECTED);
                Log.d(TAG, "BluetoothHeadsetBroadcastReceiver.onReceive: "
                        + "a=ACTION_AUDIO_STATE_CHANGED, "
                        + "s=" + SoraBluetoothManager.stateToString(state) + ", "
                        + "sb=" + isInitialStickyBroadcast());
                if (state == BluetoothHeadset.STATE_AUDIO_CONNECTED) {
                    updateAudioDeviceState();
                } else if (state == BluetoothHeadset.STATE_AUDIO_DISCONNECTED) {
                    updateAudioDeviceState();
                }
            }
        }
    }

    static SoraAudioManager2 create(Context context) {
        return new SoraAudioManager2(context);
    }

    private SoraAudioManager2(Context context) {
        super(context);
        this.bluetoothHeadsetReceiver = new BluetoothHeadsetBroadcastReceiver();
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

        super.start(observer);

        // 初期化を行う
        selectedAudioDevice = null;
        audioDevices.addAll(audioManager.getAvailableCommunicationDevices());

        // HEADSET プロファイルの状態変化を bluetoothHeadsetReceiver で取れるようにする
        IntentFilter filter = new IntentFilter();
        // bluetooth ヘッドセットの接続状態変化を取得する
        filter.addAction(BluetoothHeadset.ACTION_CONNECTION_STATE_CHANGED);
        // bluetooth ヘッドセットのオーディオの状態変化を取得する
        filter.addAction(BluetoothHeadset.ACTION_AUDIO_STATE_CHANGED);
        context.registerReceiver(bluetoothHeadsetReceiver, filter);

        // 初期化を行った状態でデバイスの設定を行う
        updateAudioDeviceState();

        registerWiredHeadsetReceiver();
    }

    // オーディオの制御を終了する
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

        context.unregisterReceiver(bluetoothHeadsetReceiver);

        audioManager.clearCommunicationDevice();

        super.stop();
    }

    // ハンズフリーかを確認する
    public boolean isHandsfree() {
        AudioDeviceInfo currentDeviceInfo = audioManager.getCommunicationDevice();
        if (currentDeviceInfo == null) {
            // 電話用スピーカーがないので、ずっとハンズフリーのようなもの
            return true;
        }
        return currentDeviceInfo.getType() == AudioDeviceInfo.TYPE_BUILTIN_SPEAKER;
    }

    // 状態に基づいてデバイスを選択する
    @Override
    void updateAudioDeviceState() {
        SoraThreadUtils.checkIsOnMainThread();
        if (!running) {
            return;
        }

        List<AudioDeviceInfo> newAudioDevices = audioManager.getAvailableCommunicationDevices();

        AudioDeviceInfo earpieceDevice = null;
        AudioDeviceInfo speakerPhoneDevice = null;
        AudioDeviceInfo wiredHeadsetDevice = null;
        AudioDeviceInfo bluetoothHeadsetDevice = null;
        // 新しいデバイスを探す
        for (AudioDeviceInfo newDevice : newAudioDevices) {
            Log.d(TAG, "newDevice Type: " + newDevice.getType());
            if (!audioDevices.contains(newDevice)) {
                // 新しいデバイス
                if (newDevice.getType() == AudioDeviceInfo.TYPE_WIRED_HEADSET ||
                        newDevice.getType() == AudioDeviceInfo.TYPE_USB_HEADSET ||
                        newDevice.getType() == AudioDeviceInfo.TYPE_BLUETOOTH_SCO ||
                        newDevice.getType() == AudioDeviceInfo.TYPE_BLE_HEADSET) {
                    // 新規デバイスが接続された場合はハンズフリーは解除する
                    isSetHandsfree = false;
                    lastConnectedAudioDevice = newDevice;
                }
            }
            switch (newDevice.getType()) {
                case AudioDeviceInfo.TYPE_BUILTIN_EARPIECE:
                    earpieceDevice = newDevice;
                    break;
                case AudioDeviceInfo.TYPE_BUILTIN_SPEAKER:
                    speakerPhoneDevice = newDevice;
                    break;
                case AudioDeviceInfo.TYPE_WIRED_HEADSET:
                case AudioDeviceInfo.TYPE_USB_HEADSET:
                    wiredHeadsetDevice = newDevice;
                    break;
                case AudioDeviceInfo.TYPE_BLUETOOTH_SCO:
                case AudioDeviceInfo.TYPE_BLE_HEADSET:
                    bluetoothHeadsetDevice = newDevice;
                    break;
                default:
                    Log.w(TAG, "Not supported audio device type: " + newDevice.getType());
                    break;
            }
        }

        // 古いリストを新しいリストで更新
        audioDevices.clear();
        audioDevices.addAll(newAudioDevices);

        AudioDeviceInfo newAudioDevice = null;
        if (isSetHandsfree) {
            newAudioDevice = speakerPhoneDevice;
        } else if (audioDevices.contains(lastConnectedAudioDevice)) {
            // 最後に接続されたデバイスがあるようなら、それを優先する
            newAudioDevice = lastConnectedAudioDevice;
        } else if (wiredHeadsetDevice != null) {
            // 最後に接続されたデバイスがないときは 有線、Bluetooth、受話用、ラウドの順であたる
            newAudioDevice = wiredHeadsetDevice;
        } else if (bluetoothHeadsetDevice != null) {
            newAudioDevice = bluetoothHeadsetDevice;
        } else if (earpieceDevice != null) {
            newAudioDevice = earpieceDevice;
        } else {
            newAudioDevice = speakerPhoneDevice;
        }
        if (newAudioDevice == null) {
            Log.e(TAG, "No supported audio device was found");
            return;
        }
        if (newAudioDevice != selectedAudioDevice) {
            Log.d(TAG, "New device status: "
                    + "available=" + audioDevices + ", "
                    + "selected=" + newAudioDevice);
            audioManager.setCommunicationDevice(newAudioDevice);
            selectedAudioDevice = newAudioDevice;
            if (onChangeRouteObserver != null) {
                onChangeRouteObserver.OnChangeRoute();
            }
        }
    }
}
