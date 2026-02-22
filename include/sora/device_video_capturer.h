/*
 *  Copyright (c) 2013 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */
#ifndef SORA_DEVICE_VIDEO_CAPTURER_H_
#define SORA_DEVICE_VIDEO_CAPTURER_H_

#include <cstddef>
#include <optional>
#include <string>

// WebRTC
#include <api/scoped_refptr.h>
#include <api/video/video_frame.h>
#include <api/video/video_sink_interface.h>
#include <modules/video_capture/video_capture.h>
#include <modules/video_capture/video_capture_defines.h>

#include "scalable_track_source.h"

namespace sora {

struct DeviceVideoCapturerConfig : ScalableVideoTrackSourceConfig {
  int width = 0;
  int height = 0;
  int target_fps = 0;
  // これが空以外だったらデバイス名から検索する
  std::string device_name;
  // これが非noneならデバイスインデックスから検索する
  // device_name と device_index どちらも指定が無い場合は 0 番から順に作成していく
  std::optional<int> device_index = 0;
};

// webrtc::VideoCaptureModule を使ったデバイスキャプチャラ。
// このキャプチャラでは動かない環境もあるため、このキャプチャラを直接利用する必要は無い。
// 様々な環境で動作するデバイスキャプチャラを利用したい場合、
// CreateCameraDeviceCapturer 関数を利用して生成するのが良い。
class DeviceVideoCapturer
    : public ScalableVideoTrackSource,
      public webrtc::VideoSinkInterface<webrtc::VideoFrame> {
 public:
  static webrtc::scoped_refptr<DeviceVideoCapturer> Create(
      const DeviceVideoCapturerConfig& config);
  DeviceVideoCapturer(const DeviceVideoCapturerConfig& config);
  virtual ~DeviceVideoCapturer();

 private:
  bool Init(size_t width,
            size_t height,
            size_t target_fps,
            size_t capture_device_index);
  void Destroy();

  // webrtc::VideoSinkInterface interface.
  void OnFrame(const webrtc::VideoFrame& frame) override;

  int LogDeviceInfo();
  int GetDeviceIndex(const std::string& device);

  webrtc::scoped_refptr<webrtc::VideoCaptureModule> vcm_;
  webrtc::VideoCaptureCapability capability_;
};

}  // namespace sora

#endif
