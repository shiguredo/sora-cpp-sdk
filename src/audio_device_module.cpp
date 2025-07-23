#include "sora/audio_device_module.h"

// WebRTC
#include <api/audio/audio_device.h>
#include <api/audio/create_audio_device_module.h>
#include <api/scoped_refptr.h>

#if defined(SORA_CPP_SDK_WINDOWS)
#include <modules/audio_device/include/audio_device_factory.h>
#endif

#if defined(SORA_CPP_SDK_ANDROID)
#include <jni.h>
#include <sdk/android/native_api/audio_device_module/audio_device_android.h>
#endif

namespace sora {

webrtc::scoped_refptr<webrtc::AudioDeviceModule> CreateAudioDeviceModule(
    const AudioDeviceModuleConfig& config) {
  if (config.audio_layer == webrtc::AudioDeviceModule::kDummyAudio) {
    return webrtc::CreateAudioDeviceModule(
        config.env, webrtc::AudioDeviceModule::kDummyAudio);
  }

#if defined(SORA_CPP_SDK_WINDOWS)
  return webrtc::CreateWindowsCoreAudioAudioDeviceModule(
      &config.env.task_queue_factory());
#elif defined(SORA_CPP_SDK_ANDROID)
  return webrtc::CreateJavaAudioDeviceModule(
      (JNIEnv*)config.jni_env, config.env, (jobject)config.application_context);
#else
  return webrtc::CreateAudioDeviceModule(config.env, config.audio_layer);
#endif
}

}  // namespace sora