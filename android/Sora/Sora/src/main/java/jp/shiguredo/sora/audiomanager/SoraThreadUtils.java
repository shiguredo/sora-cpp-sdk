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

import android.os.Looper;
import android.os.Handler;

class SoraThreadUtils {
    public static void checkIsOnMainThread() {
        if (Looper.myLooper() != Looper.getMainLooper()) {
            throw new IllegalStateException("Not on main thread!");
        }
    }
    public static boolean runOnMainThread(Runnable action) {
        if (Looper.myLooper() != Looper.getMainLooper()) {
            new Handler(Looper.getMainLooper()).post(action);
            return false;
        }
        return true;
    }
}
