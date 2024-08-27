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
import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
import android.content.IntentFilter;
import android.content.pm.PackageManager;
import android.media.AudioAttributes;
import android.media.AudioFocusRequest;
import android.media.AudioManager;
import android.os.Build;
import android.util.Log;

abstract class SoraAudioManagerBase implements SoraAudioManager {
    private static final String TAG = "SoraAudioManager";
    protected final Context context;
    protected final AudioManager audioManager;
    protected final BroadcastReceiver wiredHeadsetReceiver;
    protected boolean running;
    private int savedAudioMode = AudioManager.MODE_INVALID;
    private boolean savedIsMicrophoneMute;
    protected boolean hasWiredHeadset;
    protected boolean isSetHandsfree;
    protected Object audioFocus;
    protected OnChangeRouteObserver  onChangeRouteObserver;

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

    protected SoraAudioManagerBase(Context context) {
        this.context = context;
        audioManager = (AudioManager) context.getSystemService(Context.AUDIO_SERVICE);
        wiredHeadsetReceiver = new WiredHeadsetReceiver();
        running = false;
    }

    @Override
    public void start(OnChangeRouteObserver  observer) {
        savedAudioMode = audioManager.getMode();
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
        // MODE_IN_CALL か MODE_IN_COMMUNICATION でなければ setSpeakerphoneOn が使えない
        audioManager.setMode(AudioManager.MODE_IN_COMMUNICATION);

        // マイクのミュートは解除する
        setMicrophoneMute(false);

        // 初期化を行う
        isSetHandsfree = false;
    }

    // 有線ヘッドセットの接続を通知するレシーバーを登録する
    protected void registerWiredHeadsetReceiver() {
        context.registerReceiver(
                wiredHeadsetReceiver,
                new IntentFilter(Intent.ACTION_HEADSET_PLUG));
    }

    @Override
    @SuppressLint("WrongConstant")
    public void stop() {
        // 開始時に保存していた設定に戻す
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

    // 有線ヘッドセットの接続を通知するレシーバーを解除
    protected void unregisterWiredHeadsetReceiver() {
        context.unregisterReceiver(wiredHeadsetReceiver);
    }

    @Override
    public void setHandsfree(boolean on) {
        SoraThreadUtils.checkIsOnMainThread();
        if (isSetHandsfree == on) {
            return;
        }
        isSetHandsfree = on;
        updateAudioDeviceState();
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
    protected boolean hasEarpiece() {
        return context.getPackageManager().hasSystemFeature(PackageManager.FEATURE_TELEPHONY);
    }

    // 状態に基づいてデバイスを選択する
    abstract void updateAudioDeviceState();
}
