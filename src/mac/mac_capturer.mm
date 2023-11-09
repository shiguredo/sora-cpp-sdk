/*
 *  Copyright (c) 2019 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "sora/mac/mac_capturer.h"

// WebRTC
#include <rtc_base/logging.h>

// WebRTC
#import <sdk/objc/base/RTCVideoCapturer.h>
#import <sdk/objc/components/capturer/RTCCameraVideoCapturer.h>
#import <sdk/objc/native/api/video_capturer.h>
#import <sdk/objc/native/src/objc_frame_buffer.h>

@interface RTCVideoSourceAdapter : NSObject <RTCVideoCapturerDelegate>
@property(nonatomic) sora::MacCapturer* capturer;
@end

@implementation RTCVideoSourceAdapter
@synthesize capturer = _capturer;

- (void)capturer:(RTCVideoCapturer*)capturer
    didCaptureVideoFrame:(RTCVideoFrame*)frame {
  const int64_t timestamp_us = frame.timeStampNs / rtc::kNumNanosecsPerMicrosec;
  rtc::scoped_refptr<webrtc::VideoFrameBuffer> buffer =
      rtc::make_ref_counted<webrtc::ObjCFrameBuffer>(frame.buffer);
  _capturer->OnFrame(webrtc::VideoFrame::Builder()
                         .set_video_frame_buffer(buffer)
                         .set_rotation((webrtc::VideoRotation)frame.rotation)
                         .set_timestamp_us(timestamp_us)
                         .build());
}

@end

namespace {

AVCaptureDeviceFormat* SelectClosestFormat(AVCaptureDevice* device,
                                           size_t width,
                                           size_t height) {
  NSArray<AVCaptureDeviceFormat*>* formats =
      [RTCCameraVideoCapturer supportedFormatsForDevice:device];
  AVCaptureDeviceFormat* selectedFormat = nil;
  int currentDiff = INT_MAX;
  for (AVCaptureDeviceFormat* format in formats) {
    CMVideoDimensions dimension =
        CMVideoFormatDescriptionGetDimensions(format.formatDescription);
    int diff = std::abs((int64_t)width - dimension.width) +
               std::abs((int64_t)height - dimension.height);
    if (diff < currentDiff) {
      selectedFormat = format;
      currentDiff = diff;
    }
  }
  return selectedFormat;
}

}  // namespace

namespace sora {

MacCapturer::MacCapturer(const MacCapturerConfig& config) : ScalableVideoTrackSource(config) {
  RTC_LOG(LS_INFO) << "MacCapturer width=" << config.width << ", height=" << config.height
                   << ", target_fps=" << config.target_fps;

  adapter_ = [[RTCVideoSourceAdapter alloc] init];
  adapter_.capturer = this;

  capturer_ = [[RTCCameraVideoCapturer alloc] initWithDelegate:adapter_];
  AVCaptureDeviceFormat* format = SelectClosestFormat(config.device, config.width, config.height);
  [capturer_ startCaptureWithDevice:config.device format:format fps:config.target_fps];
}

rtc::scoped_refptr<MacCapturer> MacCapturer::Create(const MacCapturerConfig& config) {
  MacCapturerConfig c = config;
  if (c.device == nullptr) {
    AVCaptureDevice* device = FindVideoDevice(c.device_name);
    if (!device) {
      RTC_LOG(LS_ERROR) << "Failed to create MacCapture";
      return nullptr;
    }
    c.device = device;
  }
  return rtc::make_ref_counted<MacCapturer>(c);
}

static NSArray<AVCaptureDevice*>* captureDevices() {
// macOS では USB で接続されたカメラも取得する
#if defined(SORA_CPP_SDK_MACOS)
  // AVCaptureDeviceTypeExternal の利用には macOS 14 以上が必要だが、 GitHub Actions では macOS 14 が利用出来ないため一時的に古い API を使う
  // AVCaptureDeviceDiscoverySession *session = [AVCaptureDeviceDiscoverySession
  // discoverySessionWithDeviceTypes:@[ AVCaptureDeviceTypeBuiltInWideAngleCamera, AVCaptureDeviceTypeExternal ]
  //                       mediaType:AVMediaTypeVideo
  //                       position:AVCaptureDevicePositionUnspecified];
  // return session.devices;
  return [AVCaptureDevice devicesWithMediaType:AVMediaTypeVideo];
#else
  AVCaptureDeviceDiscoverySession *session = [AVCaptureDeviceDiscoverySession
    discoverySessionWithDeviceTypes:@[ AVCaptureDeviceTypeBuiltInWideAngleCamera ]
                          mediaType:AVMediaTypeVideo
                           position:AVCaptureDevicePositionUnspecified];
  return session.devices;
# endif
}

bool MacCapturer::EnumVideoDevice(
    std::function<void(std::string, std::string)> f) {
  NSArray<AVCaptureDevice*>* devices = captureDevices();
  [devices enumerateObjectsUsingBlock:^(AVCaptureDevice* device, NSUInteger i,
                                        BOOL* stop) {
    f([device.localizedName UTF8String], [device.uniqueID UTF8String]);
  }];
  return true;
}

AVCaptureDevice* MacCapturer::FindVideoDevice(
    const std::string& specifiedVideoDevice) {
  // Device の決定ロジックは ffmpeg の avfoundation と同じ仕様にする
  // https://www.ffmpeg.org/ffmpeg-devices.html#avfoundation

  size_t capture_device_index = SIZE_T_MAX;
  NSArray<AVCaptureDevice*>* devices = captureDevices();
  [devices enumerateObjectsUsingBlock:^(AVCaptureDevice* device, NSUInteger i,
                                        BOOL* stop) {
    // 便利なのでデバイスの一覧をログに出力しておく
    RTC_LOG(LS_INFO) << "video device found: [" << i
                     << "] device_name=" << [device.localizedName UTF8String];
  }];

  // video-device オプション未指定、空白、"default", "0" の場合はデフォルトデバイスを返す
  if (specifiedVideoDevice.empty() || specifiedVideoDevice == "default" ||
      specifiedVideoDevice == "0") {
    capture_device_index = 0;
  } else {
    NSUInteger selected_index =
        [devices indexOfObjectPassingTest:^BOOL(AVCaptureDevice* device,
                                                NSUInteger i, BOOL* stop) {
          // デバイス番号を優先して検索
          if (specifiedVideoDevice == [@(i).stringValue UTF8String]) {
            return YES;
          }

          // デバイス名は前方一致検索
          std::string device_name = [device.localizedName UTF8String];
          if (device_name.find(specifiedVideoDevice) == 0) {
            return YES;
          }

          // ユニークIDは完全一致
          std::string unique_id = [device.uniqueID UTF8String];
          if (specifiedVideoDevice == unique_id) {
            return YES;
          }

          return NO;
        }];

    if (selected_index != NSNotFound) {
      capture_device_index = selected_index;
    }
  }

  if (capture_device_index != SIZE_T_MAX) {
    AVCaptureDevice* device = [captureDevices() objectAtIndex:capture_device_index];
    RTC_LOG(LS_INFO) << "selected video device: [" << capture_device_index
                     << "] device_name=" << [device.localizedName UTF8String];
    return device;
  }

  RTC_LOG(LS_INFO) << "no matching video device found";
  return nullptr;
}

void MacCapturer::Stop() {
  rtc::scoped_refptr<MacCapturer> self(this);
  RTC_LOG(LS_INFO) << "MacCapturer::Stop()";
  [capturer_ stopCaptureWithCompletionHandler:^{
    // self を参照することで、stopCaptureWithCompletionHandler が完了するまで
    // オブジェクトが破棄されないようにする
    RTC_LOG(LS_INFO) << "MacCapturer::Destroy() completed: self=" << (void*)self.get();
  }];
}

MacCapturer::~MacCapturer() {
  RTC_LOG(LS_INFO) << "MacCapturer::~MacCapturer()";
}

void MacCapturer::OnFrame(const webrtc::VideoFrame& frame) {
  OnCapturedFrame(frame);
}

}
