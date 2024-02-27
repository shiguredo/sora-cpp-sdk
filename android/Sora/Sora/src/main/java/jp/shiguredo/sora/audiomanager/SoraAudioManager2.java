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
import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
import android.content.IntentFilter;
import android.media.AudioDeviceInfo;
import android.media.AudioManager;
import android.os.Build;
import android.util.Log;

import java.util.ArrayList;
import java.util.List;

@TargetApi(Build.VERSION_CODES.S)
public class SoraAudioManager2 extends SoraAudioManagerBase {
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
            if (action.equals(AudioManager.ACTION_SCO_AUDIO_STATE_UPDATED)) {
                final int state =
                        intent.getIntExtra(AudioManager.EXTRA_SCO_AUDIO_STATE,
                                AudioManager.SCO_AUDIO_STATE_DISCONNECTED);
                Log.d(TAG, "BluetoothHeadsetBroadcastReceiver.onReceive: "
                        + "a=ACTION_SCO_AUDIO_STATE_UPDATED, "
                        + "s=" + SoraAudioManager2.stateToString(state) + ", "
                        + "sb=" + isInitialStickyBroadcast());
                if (state == AudioManager.SCO_AUDIO_STATE_CONNECTED) {
                    // SCO 接続された
                    updateAudioDeviceState();
                } else if (state == AudioManager.SCO_AUDIO_STATE_DISCONNECTED) {
                    // SCO が切られた
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
        // bluetooth SCO の状態変化を取得する
        filter.addAction(AudioManager.ACTION_SCO_AUDIO_STATE_UPDATED);
        context.registerReceiver(bluetoothHeadsetReceiver, filter);

        // 初期化を行った状態でデバイスの設定を行う
        updateAudioDeviceState();

        registerWiredHeadsetReceiver();
    }

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

    @Override
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
        if (!newAudioDevice.equals(selectedAudioDevice)) {
            Log.d(TAG, "New device status: "
                    + "available=[" + audioDeviceInfosToString(audioDevices) + "], "
                    + "selected=" + audioDeviceInfoTypeToString(newAudioDevice.getType()));
            audioManager.setCommunicationDevice(newAudioDevice);
            selectedAudioDevice = newAudioDevice;
            if (onChangeRouteObserver != null) {
                onChangeRouteObserver.OnChangeRoute();
            }
        }
    }

    static String stateToString(int state) {
        switch (state) {
            case AudioManager.SCO_AUDIO_STATE_DISCONNECTED:
                return "DISCONNECTED";
            case AudioManager.SCO_AUDIO_STATE_CONNECTED:
                return "CONNECTED";
            case AudioManager.SCO_AUDIO_STATE_CONNECTING:
                return "CONNECTING";
            case AudioManager.SCO_AUDIO_STATE_ERROR:
                return "ERROR";
            default:
                return "INVALID";
        }
    }

    static String audioDeviceInfosToString(List<AudioDeviceInfo> newAudioDevices) {
        StringBuilder devicesStringBuilder = new StringBuilder();

        for (AudioDeviceInfo device : newAudioDevices) {
            String deviceTypeString = audioDeviceInfoTypeToString(device.getType());

            devicesStringBuilder.append(deviceTypeString).append(", ");
        }

        // 末尾のカンマとスペースを削除
        if (devicesStringBuilder.length() > 0) {
            devicesStringBuilder.setLength(devicesStringBuilder.length() - 2);
        }

        return devicesStringBuilder.toString();
    }

    static String audioDeviceInfoTypeToString(int type) {
        switch (type) {
            case AudioDeviceInfo.TYPE_AUX_LINE:
                return "AUX_LINE";
            case AudioDeviceInfo.TYPE_BLE_BROADCAST:
                return "BLE_BROADCAST";
            case AudioDeviceInfo.TYPE_BLE_HEADSET:
                return "BLE_HEADSET";
            case AudioDeviceInfo.TYPE_BLE_SPEAKER:
                return "BLE_SPEAKER";
            case AudioDeviceInfo.TYPE_BLUETOOTH_A2DP:
                return "BLUETOOTH_A2DP";
            case AudioDeviceInfo.TYPE_BLUETOOTH_SCO:
                return "BLUETOOTH_SCO";
            case AudioDeviceInfo.TYPE_BUILTIN_EARPIECE:
                return "BUILTIN_EARPIECE";
            case AudioDeviceInfo.TYPE_BUILTIN_MIC:
                return "BUILTIN_MIC";
            case AudioDeviceInfo.TYPE_BUILTIN_SPEAKER:
                return "BUILTIN_SPEAKER";
            case AudioDeviceInfo.TYPE_BUILTIN_SPEAKER_SAFE:
                return "BUILTIN_SPEAKER_SAFE";
            case AudioDeviceInfo.TYPE_BUS:
                return "BUS";
            case AudioDeviceInfo.TYPE_DOCK:
                return "DOCK";
            case AudioDeviceInfo.TYPE_DOCK_ANALOG:
                return "DOCK_ANALOG";
            case AudioDeviceInfo.TYPE_FM:
                return "FM";
            case AudioDeviceInfo.TYPE_FM_TUNER:
                return "FM_TUNER";
            case AudioDeviceInfo.TYPE_HDMI:
                return "HDMI";
            case AudioDeviceInfo.TYPE_HDMI_ARC:
                return "HDMI_ARC";
            case AudioDeviceInfo.TYPE_HDMI_EARC:
                return "HDMI_EARC";
            case AudioDeviceInfo.TYPE_HEARING_AID:
                return "HEARING_AID";
            case AudioDeviceInfo.TYPE_IP:
                return "IP";
            case AudioDeviceInfo.TYPE_LINE_ANALOG:
                return "LINE_ANALOG";
            case AudioDeviceInfo.TYPE_LINE_DIGITAL:
                return "LINE_DIGITAL";
            case AudioDeviceInfo.TYPE_REMOTE_SUBMIX:
                return "REMOTE_SUBMIX";
            case AudioDeviceInfo.TYPE_TELEPHONY:
                return "TELEPHONY";
            case AudioDeviceInfo.TYPE_TV_TUNER:
                return "TV_TUNER";
            case AudioDeviceInfo.TYPE_UNKNOWN:
                return "UNKNOWN";
            case AudioDeviceInfo.TYPE_USB_ACCESSORY:
                return "USB_ACCESSORY";
            case AudioDeviceInfo.TYPE_USB_DEVICE:
                return "USB_DEVICE";
            case AudioDeviceInfo.TYPE_USB_HEADSET:
                return "USB_HEADSET";
            case AudioDeviceInfo.TYPE_WIRED_HEADPHONES:
                return "WIRED_HEADPHONES";
            case AudioDeviceInfo.TYPE_WIRED_HEADSET:
                return "WIRED_HEADSET";
            default:
                return "INVALID";
        }
    }
}
