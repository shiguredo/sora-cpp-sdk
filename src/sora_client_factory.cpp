#include "sora/sora_client_factory.h"

// WebRTC
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

#include "sora/audio_device_module.h"
#include "sora/camera_device_capturer.h"
#include "sora/java_context.h"
#include "sora/sora_audio_decoder_factory.h"
#include "sora/sora_audio_encoder_factory.h"
#include "sora/sora_peer_connection_factory.h"
#include "sora/sora_video_decoder_factory.h"
#include "sora/sora_video_encoder_factory.h"

namespace sora {

std::shared_ptr<SoraClientFactory> SoraClientFactory::Create(
    const SoraClientFactoryConfig& config) {
  std::shared_ptr<SoraClientFactory> f = std::make_shared<SoraClientFactory>();

  f->config_ = config;
  f->network_thread_ = rtc::Thread::CreateWithSocketServer();
  f->network_thread_->Start();
  f->worker_thread_ = rtc::Thread::Create();
  f->worker_thread_->Start();
  f->signaling_thread_ = rtc::Thread::Create();
  f->signaling_thread_->Start();

  webrtc::PeerConnectionFactoryDependencies dependencies;
  dependencies.network_thread = f->network_thread_.get();
  dependencies.worker_thread = f->worker_thread_.get();
  dependencies.signaling_thread = f->signaling_thread_.get();
  dependencies.task_queue_factory = webrtc::CreateDefaultTaskQueueFactory();
  dependencies.call_factory = webrtc::CreateCallFactory();
  dependencies.event_log_factory =
      absl::make_unique<webrtc::RtcEventLogFactory>(
          dependencies.task_queue_factory.get());

  void* env = sora::GetJNIEnv();

  // media_dependencies
  cricket::MediaEngineDependencies media_dependencies;
  media_dependencies.task_queue_factory = dependencies.task_queue_factory.get();
  media_dependencies.adm = f->worker_thread_->BlockingCall([&] {
    sora::AudioDeviceModuleConfig config;
    if (!f->config_.use_audio_device) {
      config.audio_layer = webrtc::AudioDeviceModule::kDummyAudio;
    }
    config.task_queue_factory = dependencies.task_queue_factory.get();
    config.jni_env = sora::GetJNIEnv();
    if (f->config_.get_android_application_context) {
      config.application_context =
          f->config_.get_android_application_context(config.jni_env);
    }
    return sora::CreateAudioDeviceModule(config);
  });

  media_dependencies.audio_encoder_factory =
      sora::CreateBuiltinAudioEncoderFactory();
  media_dependencies.audio_decoder_factory =
      sora::CreateBuiltinAudioDecoderFactory();

  auto cuda_context = sora::CudaContext::Create();
  {
    auto config =
        f->config_.use_hardware_encoder
            ? sora::GetDefaultVideoEncoderFactoryConfig(cuda_context, env)
            : sora::GetSoftwareOnlyVideoEncoderFactoryConfig();
    config.use_simulcast_adapter = true;
    media_dependencies.video_encoder_factory =
        absl::make_unique<sora::SoraVideoEncoderFactory>(std::move(config));
  }
  {
    auto config =
        f->config_.use_hardware_encoder
            ? sora::GetDefaultVideoDecoderFactoryConfig(cuda_context, env)
            : sora::GetSoftwareOnlyVideoDecoderFactoryConfig();
    media_dependencies.video_decoder_factory =
        absl::make_unique<sora::SoraVideoDecoderFactory>(std::move(config));
  }

  media_dependencies.audio_mixer = nullptr;
  media_dependencies.audio_processing =
      webrtc::AudioProcessingBuilder().Create();

  if (f->config_.configure_media_dependencies) {
    f->config_.configure_media_dependencies(media_dependencies);
  }

  dependencies.media_engine =
      cricket::CreateMediaEngine(std::move(media_dependencies));

  if (f->config_.configure_dependencies) {
    f->config_.configure_dependencies(dependencies);
  }

  f->factory_ = sora::CreateModularPeerConnectionFactoryWithContext(
      std::move(dependencies), f->connection_context_);

  if (f->factory_ == nullptr) {
    RTC_LOG(LS_ERROR) << "Failed to create PeerConnectionFactory";
    return nullptr;
  }

  webrtc::PeerConnectionFactoryInterface::Options factory_options;
  factory_options.disable_encryption = false;
  factory_options.ssl_max_version = rtc::SSL_PROTOCOL_DTLS_12;
  factory_options.crypto_options.srtp.enable_gcm_crypto_suites = true;
  f->factory_->SetOptions(factory_options);

  return f;
}

}  // namespace sora