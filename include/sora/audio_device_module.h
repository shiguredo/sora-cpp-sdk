#ifndef SORA_AUDIO_DEVICE_MODULE_H_
#define SORA_AUDIO_DEVICE_MODULE_H_

// WebRTC
#include <api/audio/audio_device.h>
#include <api/environment/environment.h>
#include <api/environment/environment_factory.h>
#include <api/scoped_refptr.h>

namespace sora {

struct AudioDeviceModuleConfig {
  webrtc::Environment env = webrtc::CreateEnvironment();
  webrtc::AudioDeviceModule::AudioLayer audio_layer =
      webrtc::AudioDeviceModule::kPlatformDefaultAudio;

  // 以下は Android のみ必要かつ必須
  void* jni_env = nullptr;
  void* application_context = nullptr;
};

// オーディオデバイスを使った ADM を生成する。
// マイクからの録音と、スピーカーからの再生ができるようになる。
webrtc::scoped_refptr<webrtc::AudioDeviceModule> CreateAudioDeviceModule(
    const AudioDeviceModuleConfig& config);

}  // namespace sora

#endif