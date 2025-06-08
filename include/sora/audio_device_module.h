#ifndef SORA_AUDIO_DEVICE_MODULE_H_
#define SORA_AUDIO_DEVICE_MODULE_H_

#include <api/task_queue/task_queue_factory.h>
#include <modules/audio_device/include/audio_device.h>

namespace sora {

struct AudioDeviceModuleConfig {
  webrtc::AudioDeviceModule::AudioLayer audio_layer =
      webrtc::AudioDeviceModule::kPlatformDefaultAudio;
  webrtc::TaskQueueFactory* task_queue_factory = nullptr;

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