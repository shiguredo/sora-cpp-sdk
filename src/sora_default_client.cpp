#include "sora/sora_default_client.h"

// WebRTC
#include <api/audio_codecs/builtin_audio_decoder_factory.h>
#include <api/audio_codecs/builtin_audio_encoder_factory.h>
#include <api/create_peerconnection_factory.h>
#include <api/rtc_event_log/rtc_event_log_factory.h>
#include <api/task_queue/default_task_queue_factory.h>
#include <media/engine/webrtc_media_engine.h>
#include <modules/audio_device/include/audio_device.h>
#include <modules/audio_device/include/audio_device_factory.h>
#include <modules/audio_processing/include/audio_processing.h>
#include <modules/video_capture/video_capture.h>
#include <modules/video_capture/video_capture_factory.h>
#include <pc/video_track_source_proxy.h>
#include <rtc_base/logging.h>
#include <rtc_base/ssl_adapter.h>

#ifdef _WIN32
#include <rtc_base/win/scoped_com_initializer.h>
#endif

#include "sora/audio_device_module.h"
#include "sora/camera_device_capturer.h"
#include "sora/java_context.h"
#include "sora/sora_video_decoder_factory.h"
#include "sora/sora_video_encoder_factory.h"

namespace sora {

SoraDefaultClient::SoraDefaultClient(SoraDefaultClientConfig config)
    : config_(config) {}

bool SoraDefaultClient::Configure() {
  rtc::InitializeSSL();

  network_thread_ = rtc::Thread::CreateWithSocketServer();
  network_thread_->Start();
  worker_thread_ = rtc::Thread::Create();
  worker_thread_->Start();
  signaling_thread_ = rtc::Thread::Create();
  signaling_thread_->Start();

  webrtc::PeerConnectionFactoryDependencies dependencies;
  dependencies.network_thread = network_thread_.get();
  dependencies.worker_thread = worker_thread_.get();
  dependencies.signaling_thread = signaling_thread_.get();
  dependencies.task_queue_factory = webrtc::CreateDefaultTaskQueueFactory();
  dependencies.call_factory = webrtc::CreateCallFactory();
  dependencies.event_log_factory =
      absl::make_unique<webrtc::RtcEventLogFactory>(
          dependencies.task_queue_factory.get());

  void* env = sora::GetJNIEnv();

  // media_dependencies
  cricket::MediaEngineDependencies media_dependencies;
  media_dependencies.task_queue_factory = dependencies.task_queue_factory.get();
  media_dependencies.adm =
      worker_thread_->Invoke<rtc::scoped_refptr<webrtc::AudioDeviceModule>>(
          RTC_FROM_HERE, [&] {
            sora::AudioDeviceModuleConfig config;
            if (!config_.use_audio_deivce) {
              config.audio_layer = webrtc::AudioDeviceModule::kDummyAudio;
            }
            config.task_queue_factory = dependencies.task_queue_factory.get();
            config.jni_env = env;
            config.application_context = GetAndroidApplicationContext(env);
            return sora::CreateAudioDeviceModule(config);
          });

  media_dependencies.audio_encoder_factory =
      webrtc::CreateBuiltinAudioEncoderFactory();
  media_dependencies.audio_decoder_factory =
      webrtc::CreateBuiltinAudioDecoderFactory();

  auto cuda_context = sora::CudaContext::Create();
  {
    auto config =
        config_.use_hardware_encoder
            ? sora::GetDefaultVideoEncoderFactoryConfig(cuda_context, env)
            : sora::GetSoftwareOnlyVideoEncoderFactoryConfig();
    config.use_simulcast_adapter = true;
    media_dependencies.video_encoder_factory =
        absl::make_unique<sora::SoraVideoEncoderFactory>(std::move(config));
  }
  {
    auto config =
        config_.use_hardware_encoder
            ? sora::GetDefaultVideoDecoderFactoryConfig(cuda_context, env)
            : sora::GetSoftwareOnlyVideoDecoderFactoryConfig();
    media_dependencies.video_decoder_factory =
        absl::make_unique<sora::SoraVideoDecoderFactory>(std::move(config));
  }

  media_dependencies.audio_mixer = nullptr;
  media_dependencies.audio_processing =
      webrtc::AudioProcessingBuilder().Create();

  dependencies.media_engine =
      cricket::CreateMediaEngine(std::move(media_dependencies));

  ConfigureDependencies(dependencies);

  factory_ =
      webrtc::CreateModularPeerConnectionFactory(std::move(dependencies));

  if (factory_ == nullptr) {
    RTC_LOG(LS_ERROR) << "Failed to create PeerConnectionFactory";
    return false;
  }

  webrtc::PeerConnectionFactoryInterface::Options factory_options;
  factory_options.disable_encryption = false;
  factory_options.ssl_max_version = rtc::SSL_PROTOCOL_DTLS_12;
  factory_options.crypto_options.srtp.enable_gcm_crypto_suites = true;
  factory_->SetOptions(factory_options);

  OnConfigured();

  return true;
}

}  // namespace sora