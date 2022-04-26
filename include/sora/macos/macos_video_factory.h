/*
 *  Copyright 2017 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#ifndef SORA_MACOS_MACOS_VIDEO_FACTORY_H_
#define SORA_MACOS_MACOS_VIDEO_FACTORY_H_

#include <memory>

// WebRTC
#include <api/video_codecs/video_decoder_factory.h>
#include <api/video_codecs/video_encoder_factory.h>

namespace sora {

std::unique_ptr<webrtc::VideoEncoderFactory> CreateMacosVideoEncoderFactory();
std::unique_ptr<webrtc::VideoDecoderFactory> CreateMacosVideoDecoderFactory();

}  // namespace sora

#endif
