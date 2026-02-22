/*
 *  Copyright 2017 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "sora/mac/mac_video_factory.h"

// WebRTC
#include <sdk/objc/native/api/video_decoder_factory.h>
#include <sdk/objc/native/api/video_encoder_factory.h>

// WebRTC
#import <sdk/objc/components/video_codec/RTCDefaultVideoDecoderFactory.h>
#import <sdk/objc/components/video_codec/RTCDefaultVideoEncoderFactory.h>

namespace sora {

std::unique_ptr<webrtc::VideoEncoderFactory> CreateMacVideoEncoderFactory() {
  return webrtc::ObjCToNativeVideoEncoderFactory(
      [[RTCDefaultVideoEncoderFactory alloc] init]);
}

std::unique_ptr<webrtc::VideoDecoderFactory> CreateMacVideoDecoderFactory() {
  return webrtc::ObjCToNativeVideoDecoderFactory(
      [[RTCDefaultVideoDecoderFactory alloc] init]);
}

}  // namespace sora
