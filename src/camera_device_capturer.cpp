#include "sora/camera_device_capturer.h"

// WebRTC
#include <api/media_stream_interface.h>
#include <api/scoped_refptr.h>

#if defined(__APPLE__)
#include "sora/mac/mac_capturer.h"
#elif defined(SORA_CPP_SDK_ANDROID)
#include "sora/android/android_capturer.h"
#elif (defined(SORA_CPP_SDK_UBUNTU_2004) ||  \
       defined(SORA_CPP_SDK_UBUNTU_2204)) && \
    defined(USE_NVCODEC_ENCODER)
#include "sora/hwenc_nvcodec/nvcodec_v4l2_capturer.h"
#elif defined(__linux__)
#include "sora/v4l2/v4l2_video_capturer.h"
#else
#include "sora/device_video_capturer.h"
#endif
#if defined(USE_V4L2_ENCODER)
#include "sora/hwenc_v4l2/libcamera_capturer.h"
#endif

namespace sora {

webrtc::scoped_refptr<webrtc::VideoTrackSourceInterface>
CreateCameraDeviceCapturer(const CameraDeviceCapturerConfig& config) {
#if defined(__APPLE__)
  MacCapturerConfig c;
  c.on_frame = config.on_frame;
  c.width = config.width;
  c.height = config.height;
  c.target_fps = config.fps;
  c.device_name = config.device_name;
  return sora::MacCapturer::Create(c);
#elif defined(SORA_CPP_SDK_ANDROID)
  return sora::AndroidCapturer::Create(
      (JNIEnv*)config.jni_env, (jobject)config.application_context,
      config.signaling_thread, config.width, config.height, config.fps,
      config.device_name);
#elif (defined(SORA_CPP_SDK_UBUNTU_2004) ||  \
       defined(SORA_CPP_SDK_UBUNTU_2204)) && \
    defined(USE_NVCODEC_ENCODER)
  sora::V4L2VideoCapturerConfig v4l2_config;
  v4l2_config.on_frame = config.on_frame;
  v4l2_config.video_device = config.device_name;
  v4l2_config.width = config.width;
  v4l2_config.height = config.height;
  v4l2_config.framerate = config.fps;
  v4l2_config.force_i420 = config.force_i420;
  v4l2_config.use_native = config.use_native;
  if (config.use_native) {
    sora::NvCodecV4L2CapturerConfig nvcodec_config = v4l2_config;
    nvcodec_config.cuda_context = config.cuda_context;
    return sora::NvCodecV4L2Capturer::Create(nvcodec_config);
  } else {
    return sora::V4L2VideoCapturer::Create(v4l2_config);
  }
#elif defined(__linux__)
  sora::V4L2VideoCapturerConfig v4l2_config;
  v4l2_config.on_frame = config.on_frame;
  v4l2_config.video_device = config.device_name;
  v4l2_config.width = config.width;
  v4l2_config.height = config.height;
  v4l2_config.framerate = config.fps;
  v4l2_config.force_i420 = config.force_i420;
  v4l2_config.use_native = config.use_native;
#if defined(USE_V4L2_ENCODER)
  if (config.use_libcamera) {
    sora::LibcameraCapturerConfig libcamera_config = v4l2_config;
    libcamera_config.native_frame_output = config.libcamera_native_frame_output;
    libcamera_config.controls = config.libcamera_controls;
    return sora::LibcameraCapturer::Create(libcamera_config);
  } else {
    return sora::V4L2VideoCapturer::Create(v4l2_config);
  }
#else
  return sora::V4L2VideoCapturer::Create(v4l2_config);
#endif

#else
  DeviceVideoCapturerConfig c;
  c.on_frame = config.on_frame;
  c.width = config.width;
  c.height = config.height;
  c.target_fps = config.fps;
  c.device_name = config.device_name;
  return sora::DeviceVideoCapturer::Create(c);
#endif
}

}  // namespace sora