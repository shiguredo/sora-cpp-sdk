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

import android.content.Context;
import android.os.Build;

public class SoraAudioManagerFactory {

    public static SoraAudioManager create(Context context) {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
            return SoraAudioManager2.create(context);
        } else {
            return SoraAudioManagerLegacy.create(context);
        }
    }

    public static SoraAudioManager withMainThreadWrapper(SoraAudioManager audioManager) {
        return new MainThreadWrapper(audioManager);
    }

    public static SoraAudioManager createWithMainThreadWrapper(Context context) {
        return withMainThreadWrapper(create(context));
    }
}
