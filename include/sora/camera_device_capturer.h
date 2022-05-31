#ifndef SORA_CAMERA_DEVICE_CAPTURER_H_
#define SORA_CAMERA_DEVICE_CAPTURER_H_

#include <string>

// WebRTC
#include <api/media_stream_interface.h>
#include <rtc_base/thread.h>

#include "sora/cuda_context.h"

namespace sora {

struct CameraDeviceCapturerConfig {
  int width = 640;
  int height = 480;
  int fps = 30;
  std::string device_name;

  // Jetson と Linux の NvCodec の場合のみ利用可能
  bool use_native = false;
  bool force_i420 = false;

  // Linux の NvCodec の場合のみ利用可能
  std::shared_ptr<CudaContext> cuda_context;

  // Android の場合のみ必要かつ必須
  void* jni_env = nullptr;
  void* application_context = nullptr;
  rtc::Thread* signaling_thread = nullptr;
};

// カメラデバイスを使ったキャプチャラを生成する。
// カメラデバイスが存在するならどのプラットフォームでも動作する。
// カメラデバイスが存在しなければ nullptr を返す。
rtc::scoped_refptr<webrtc::VideoTrackSourceInterface>
CreateCameraDeviceCapturer(const CameraDeviceCapturerConfig& config);

}  // namespace sora

#endif