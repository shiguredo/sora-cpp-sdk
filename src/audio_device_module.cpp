#include "sora/audio_device_module.h"

#if defined(SORA_CPP_SDK_WINDOWS)
#include <modules/audio_device/include/audio_device_factory.h>
#endif

#if defined(SORA_CPP_SDK_ANDROID)
#include <jni.h>
#include <sdk/android/native_api/audio_device_module/audio_device_android.h>
#endif

namespace sora {

rtc::scoped_refptr<webrtc::AudioDeviceModule> CreateAudioDeviceModule(
    const AudioDeviceModuleConfig& config) {
  if (config.audio_layer == webrtc::AudioDeviceModule::kDummyAudio) {
    return webrtc::AudioDeviceModule::Create(
        webrtc::AudioDeviceModule::kDummyAudio, config.task_queue_factory);
  }

#if defined(SORA_CPP_SDK_WINDOWS)
  return webrtc::CreateWindowsCoreAudioAudioDeviceModule(
      config.task_queue_factory);
#elif defined(SORA_CPP_SDK_ANDROID)
  return webrtc::CreateJavaAudioDeviceModule(
      (JNIEnv*)config.jni_env, (jobject)config.application_context);
#else
  return webrtc::AudioDeviceModule::Create(config.audio_layer,
                                           config.task_queue_factory);
#endif
}

}  // namespace sora