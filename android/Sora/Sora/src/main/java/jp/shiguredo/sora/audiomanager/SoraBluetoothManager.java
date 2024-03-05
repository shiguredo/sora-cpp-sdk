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

import android.bluetooth.BluetoothAdapter;
import android.bluetooth.BluetoothDevice;
import android.bluetooth.BluetoothHeadset;
import android.bluetooth.BluetoothProfile;
import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
import android.content.IntentFilter;
import android.media.AudioManager;
import android.os.Handler;
import android.os.Looper;
import android.util.Log;

import java.util.List;

class SoraBluetoothManager {
    private static final String TAG = "SoraBluetoothManager";
    // Bluetooth SCO の開始/終了タイムアウト
    private static final int BLUETOOTH_SCO_TIMEOUT_MS = 4000;
    // SCO 接続試行上限
    private static final int MAX_SCO_CONNECTION_ATTEMPTS = 2;
    enum State {
        // Bluetooth が存在しないか OFF になっている
        UNINITIALIZED,
        // Headset profile の Bluetooth proxy object は存在するが Bluetooth headset は接続されていない
        // SCO は開始されていないか切断されている
        HEADSET_UNAVAILABLE,
        // Headset profile の Bluetooth proxy object が接続され Bluetooth headset も接続されている
        // しかし SCO は開始されていないか切断されている
        HEADSET_AVAILABLE,
        // リモートデバイスが Bluetooth audio SCO 接続を切断しようとしている
        SCO_DISCONNECTING,
        // リモートデバイスが Bluetooth audio SCO 接続を初期化している
        SCO_CONNECTING,
        // リモートデバイスと Bluetooth audio SCO 接続がされている
        SCO_CONNECTED
    }
    private final Context context;
    private final SoraAudioManagerLegacy soraAudioManagerLegacy;
    private final AudioManager audioManager;
    private State bluetoothState;
    private final BluetoothProfile.ServiceListener bluetoothServiceListener;
    private final BroadcastReceiver bluetoothHeadsetReceiver;
    private final Handler handler;
    private BluetoothAdapter bluetoothAdapter;
    private BluetoothHeadset bluetoothHeadset;
    private BluetoothDevice bluetoothDevice;
    int scoConnectionAttempts;

    private final Runnable bluetoothTimeoutRunnable = new Runnable() {
        @Override
        public void run() {
            bluetoothTimeout();
        }
    };

    private class BluetoothServiceListener implements BluetoothProfile.ServiceListener {
        @Override
        public void onServiceConnected(int profile, BluetoothProfile proxy) {
            if (profile != BluetoothProfile.HEADSET || bluetoothState == State.UNINITIALIZED) {
                return;
            }
            bluetoothHeadset = (BluetoothHeadset) proxy;
            updateAudioDeviceState();
        }

        @Override
        public void onServiceDisconnected(int profile) {
            if (profile != BluetoothProfile.HEADSET || bluetoothState == State.UNINITIALIZED) {
                return;
            }
            stopScoAudio();
            bluetoothHeadset = null;
            bluetoothDevice = null;
            bluetoothState = State.HEADSET_UNAVAILABLE;
            updateAudioDeviceState();
        }
    }

    private class BluetoothHeadsetBroadcastReceiver extends BroadcastReceiver {
        @Override
        public void onReceive(Context context, Intent intent) {
            if (bluetoothState == State.UNINITIALIZED) {
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
                        + "s=" + stateToString(state) + ", "
                        + "sb=" + isInitialStickyBroadcast() + ", "
                        + "BT state: " + bluetoothState);
                if (state == BluetoothHeadset.STATE_CONNECTED) {
                    // Bluetooth ヘッドセットとが接続された
                    scoConnectionAttempts = 0;
                    updateAudioDeviceState();
                } else if (state == BluetoothHeadset.STATE_DISCONNECTED) {
                    // おそらく Bluetooth が通話中に切られた
                    stopScoAudio();
                    updateAudioDeviceState();
                }
                /* SCO の状態変化 */
            } else if (action.equals(BluetoothHeadset.ACTION_AUDIO_STATE_CHANGED)) {
                final int state = intent.getIntExtra(
                        BluetoothHeadset.EXTRA_STATE, BluetoothHeadset.STATE_AUDIO_DISCONNECTED);
                Log.d(TAG, "BluetoothHeadsetBroadcastReceiver.onReceive: "
                        + "a=ACTION_AUDIO_STATE_CHANGED, "
                        + "s=" + stateToString(state) + ", "
                        + "sb=" + isInitialStickyBroadcast() + ", "
                        + "BT state: " + bluetoothState);
                if (state == BluetoothHeadset.STATE_AUDIO_CONNECTED) {
                    cancelTimer();
                    if (bluetoothState == State.SCO_CONNECTING) {
                        bluetoothState = State.SCO_CONNECTED;
                        scoConnectionAttempts = 0;
                        updateAudioDeviceState();
                    }
                } else if (state == BluetoothHeadset.STATE_AUDIO_DISCONNECTED) {
                    if (isInitialStickyBroadcast()) {
                        return;
                    }
                    updateAudioDeviceState();
                }
            }
        }
    }

    static SoraBluetoothManager create(
            Context context,
            SoraAudioManagerLegacy soraAudioManagerLegacy,
            AudioManager audioManager) {
        return new SoraBluetoothManager(context, soraAudioManagerLegacy, audioManager);
    }

    protected SoraBluetoothManager(
            Context context,
            SoraAudioManagerLegacy soraAudioManagerLegacy,
            AudioManager audioManager) {
        SoraThreadUtils.checkIsOnMainThread();
        this.context = context;
        this.soraAudioManagerLegacy = soraAudioManagerLegacy;
        this.audioManager = audioManager;
        bluetoothState = State.UNINITIALIZED;
        bluetoothServiceListener = new BluetoothServiceListener();
        bluetoothHeadsetReceiver = new BluetoothHeadsetBroadcastReceiver();
        handler = new Handler(Looper.getMainLooper());
    }

    State getState() {
        SoraThreadUtils.checkIsOnMainThread();
        return bluetoothState;
    }
    void start() {
        SoraThreadUtils.checkIsOnMainThread();
        if (bluetoothState != State.UNINITIALIZED) {
            Log.w(TAG, "Invalid BT state");
            return;
        }
        bluetoothHeadset = null;
        bluetoothDevice = null;
        scoConnectionAttempts = 0;
        bluetoothAdapter = BluetoothAdapter.getDefaultAdapter();
        // Bluetooth がサポートされているかをチェック
        if (bluetoothAdapter == null) {
            Log.w(TAG, "Device does not support Bluetooth");
            return;
        }
        // Bluetooth SCO Audio がサポートされているかをチェック
        if (!audioManager.isBluetoothScoAvailableOffCall()) {
            Log.e(TAG, "Bluetooth SCO audio is not available off call");
            return;
        }
        // HEADSET プロファイルのデバイス接続切断を bluetoothServiceListener で取れるようにする
        bluetoothAdapter.getProfileProxy(
                context,
                bluetoothServiceListener,
                BluetoothProfile.HEADSET);
        // HEADSET プロファイルの状態変化を bluetoothHeadsetReceiver で取れるようにする
        IntentFilter filter = new IntentFilter();
        // bluetooth ヘッドセットの接続状態変化を取得する
        filter.addAction(BluetoothHeadset.ACTION_CONNECTION_STATE_CHANGED);
        // bluetooth ヘッドセットのオーディオの状態変化を取得する
        filter.addAction(BluetoothHeadset.ACTION_AUDIO_STATE_CHANGED);
        context.registerReceiver(bluetoothHeadsetReceiver, filter);
        bluetoothState = State.HEADSET_UNAVAILABLE;
    }

    void stop() {
        SoraThreadUtils.checkIsOnMainThread();
        if (bluetoothAdapter == null) {
            return;
        }
        stopScoAudio();
        if (bluetoothState == State.UNINITIALIZED) {
            return;
        }
        context.unregisterReceiver(bluetoothHeadsetReceiver);
        cancelTimer();
        if (bluetoothHeadset != null) {
            bluetoothAdapter.closeProfileProxy(BluetoothProfile.HEADSET, bluetoothHeadset);
            bluetoothHeadset = null;
        }
        bluetoothAdapter = null;
        bluetoothDevice = null;
        bluetoothState = State.UNINITIALIZED;
    }

    boolean startScoAudio() {
        SoraThreadUtils.checkIsOnMainThread();
        if (scoConnectionAttempts >= MAX_SCO_CONNECTION_ATTEMPTS) {
            Log.e(TAG, "BT SCO connection fails - no more attempts");
            return false;
        }
        if (bluetoothState != State.HEADSET_AVAILABLE) {
            Log.e(TAG, "BT SCO connection fails - no headset available");
            return false;
        }
        bluetoothState = State.SCO_CONNECTING;
        /*
         * Bluetooth SCO を開始する
         * これも AudioManager#setCommunicationDevice(AudioDeviceInfo) への置き換えで無くなる
         * SCO接続の確立には数秒かかり ACTION_SCO_AUDIO_STATE_UPDATED インテントで
         * SCO_AUDIO_STATE_CONNECTED にならなければ確立されない
         * すでに接続済みの場合は startBluetoothSco ではインテントは発火しないが
         * registerReceiver を設定した際に発火する
         * ただ、その場合も startBluetoothSco は呼んでおかないと、
         * 他のプログラムが stopBluetoothSco した際に終了してしまう
         */
        audioManager.startBluetoothSco();
        // bluetooth SCO ヘッドセットの使用をリクエストする
        audioManager.setBluetoothScoOn(true);
        scoConnectionAttempts++;
        startTimer();
        return true;
    }

    void stopScoAudio() {
        SoraThreadUtils.checkIsOnMainThread();
        if (bluetoothState != State.SCO_CONNECTING && bluetoothState != State.SCO_CONNECTED) {
            return;
        }
        cancelTimer();
        // Bluetooth SCO を終了する
        audioManager.stopBluetoothSco();
        // bluetooth SCO ヘッドセットを使用しない
        audioManager.setBluetoothScoOn(false);
        bluetoothState = State.SCO_DISCONNECTING;
    }

    void updateDevice() {
        if (bluetoothState == State.UNINITIALIZED || bluetoothHeadset == null) {
            return;
        }
        List<BluetoothDevice> devices = bluetoothHeadset.getConnectedDevices();
        if (devices.isEmpty()) {
            bluetoothDevice = null;
            bluetoothState = State.HEADSET_UNAVAILABLE;
        } else {
            bluetoothDevice = devices.get(0);
            bluetoothState = State.HEADSET_AVAILABLE;
        }
    }

    private void updateAudioDeviceState() {
        SoraThreadUtils.checkIsOnMainThread();
        soraAudioManagerLegacy.updateAudioDeviceState();
    }

    private void startTimer() {
        SoraThreadUtils.checkIsOnMainThread();
        handler.postDelayed(bluetoothTimeoutRunnable, BLUETOOTH_SCO_TIMEOUT_MS);
    }

    /** Cancels any outstanding timer tasks. */
    private void cancelTimer() {
        SoraThreadUtils.checkIsOnMainThread();
        handler.removeCallbacks(bluetoothTimeoutRunnable);
    }

    private void bluetoothTimeout() {
        SoraThreadUtils.checkIsOnMainThread();
        Log.d(TAG, "bluetoothTimeout: BT state=" + bluetoothState + ", "
                + "attempts: " + scoConnectionAttempts + ", "
                + "bluetoothHeadset: " + (bluetoothHeadset != null));
        if (bluetoothState != State.SCO_CONNECTING || bluetoothHeadset == null) {
            return;
        }
        // SCO 接続に成功していないか確認する
        boolean scoConnected = false;
        List<BluetoothDevice> devices = bluetoothHeadset.getConnectedDevices();
        if (devices.size() > 0) {
            bluetoothDevice = devices.get(0);
            if (bluetoothHeadset.isAudioConnected(bluetoothDevice)) {
                Log.d(TAG, "SCO connected with " + bluetoothDevice.getName());
                scoConnected = true;
            } else {
                Log.d(TAG, "SCO is not connected with " + bluetoothDevice.getName());
            }
        }
        if (scoConnected) {
            // タイムアウトが発火したが SCO 接続には成功していた
            bluetoothState = State.SCO_CONNECTED;
            scoConnectionAttempts = 0;
        } else {
            // SCO 接続に失敗した
            stopScoAudio();
        }
        updateAudioDeviceState();
    }

    static String stateToString(int state) {
        switch (state) {
            case BluetoothAdapter.STATE_DISCONNECTED:
                return "DISCONNECTED";
            case BluetoothAdapter.STATE_CONNECTED:
                return "CONNECTED";
            case BluetoothAdapter.STATE_CONNECTING:
                return "CONNECTING";
            case BluetoothAdapter.STATE_DISCONNECTING:
                return "DISCONNECTING";
            case BluetoothAdapter.STATE_OFF:
                return "OFF";
            case BluetoothAdapter.STATE_ON:
                return "ON";
            case BluetoothAdapter.STATE_TURNING_OFF:
                return "TURNING_OFF";
            case BluetoothAdapter.STATE_TURNING_ON:
                return  "TURNING_ON";
            default:
                return "INVALID";
        }
    }
}
