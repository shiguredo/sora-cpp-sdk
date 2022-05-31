/*
 *  Copyright (c) 2019 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */
#ifndef SORA_MAC_MAC_CAPTURER_H_
#define SORA_MAC_MAC_CAPTURER_H_

#include <memory>
#include <string>
#include <vector>

// WebRTC
#include <api/media_stream_interface.h>
#include <api/scoped_refptr.h>
#include <base/RTCMacros.h>
#include <modules/video_capture/video_capture.h>
#include <rtc_base/thread.h>

#include "sora/scalable_track_source.h"

RTC_FWD_DECL_OBJC_CLASS(AVCaptureDevice);
RTC_FWD_DECL_OBJC_CLASS(RTCCameraVideoCapturer);
RTC_FWD_DECL_OBJC_CLASS(RTCVideoSourceAdapter);

namespace sora {

class MacCapturer : public ScalableVideoTrackSource {
 public:
  static rtc::scoped_refptr<MacCapturer> Create(
      size_t width,
      size_t height,
      size_t target_fps,
      const std::string& specifiedVideoDevice);
  MacCapturer(size_t width,
              size_t height,
              size_t target_fps,
              AVCaptureDevice* device);
  virtual ~MacCapturer();

  void OnFrame(const webrtc::VideoFrame& frame);

 private:
  void Destroy();

  static AVCaptureDevice* FindVideoDevice(
      const std::string& specifiedVideoDevice);

  RTCCameraVideoCapturer* capturer_;
  RTCVideoSourceAdapter* adapter_;
};

}  // namespace sora

#endif  // MAC_CAPTURER_H_
