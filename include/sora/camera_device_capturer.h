#ifndef SORA_CAMERA_DEVICE_CAPTURER_H_
#define SORA_CAMERA_DEVICE_CAPTURER_H_

#include <string>

// WebRTC
#include <api/media_stream_interface.h>
#include <rtc_base/thread.h>
#if defined(SORA_CPP_SDK_HOLOLENS2)
#include <modules/video_capture/winuwp/mrc_video_effect_definition.h>
#endif

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

#if defined(SORA_CPP_SDK_HOLOLENS2)
  std::shared_ptr<webrtc::MrcVideoEffectDefinition> mrc;
#endif
};

// カメラデバイスを使ったキャプチャラを生成する。
// カメラデバイスが存在するならどのプラットフォームでも動作する。
// カメラデバイスが存在しなければ nullptr を返す。
rtc::scoped_refptr<webrtc::VideoTrackSourceInterface>
CreateCameraDeviceCapturer(const CameraDeviceCapturerConfig& config);

}  // namespace sora

#endif