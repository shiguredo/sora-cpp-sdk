#include "sora/sora_client_context.h"

// WebRTC
#include <api/audio_codecs/builtin_audio_decoder_factory.h>
#include <api/audio_codecs/builtin_audio_encoder_factory.h>
#include <api/create_peerconnection_factory.h>
#include <api/enable_media.h>
#include <api/rtc_event_log/rtc_event_log_factory.h>
#include <api/task_queue/default_task_queue_factory.h>
#include <call/call_config.h>
#include <media/engine/webrtc_media_engine.h>
#include <modules/audio_device/include/audio_device.h>
#include <modules/audio_device/include/audio_device_factory.h>
#include <modules/audio_processing/include/audio_processing.h>
#include <modules/video_capture/video_capture.h>
#include <modules/video_capture/video_capture_factory.h>
#include <pc/media_factory.h>
#include <pc/video_track_source_proxy.h>
#include <rtc_base/logging.h>
#include <rtc_base/ssl_adapter.h>

#include "sora/audio_device_module.h"
#include "sora/camera_device_capturer.h"
#include "sora/java_context.h"
#include "sora/sora_peer_connection_factory.h"
#include "sora/sora_video_decoder_factory.h"
#include "sora/sora_video_encoder_factory.h"

namespace sora {

SoraClientContext::~SoraClientContext() {
  config_ = SoraClientContextConfig();
  connection_context_ = nullptr;
  factory_ = nullptr;
  network_thread_->Stop();
  worker_thread_->Stop();
  signaling_thread_->Stop();

  //rtc::CleanupSSL();
}

std::shared_ptr<SoraClientContext> SoraClientContext::Create(
    const SoraClientContextConfig& config,
    webrtc::Environment& env) {
  rtc::InitializeSSL();

  std::shared_ptr<SoraClientContext> c = std::make_shared<SoraClientContext>();

  c->config_ = config;
  c->network_thread_ = rtc::Thread::CreateWithSocketServer();
  c->network_thread_->Start();
  c->worker_thread_ = rtc::Thread::Create();
  c->worker_thread_->Start();
  c->signaling_thread_ = rtc::Thread::Create();
  c->signaling_thread_->Start();

  webrtc::PeerConnectionFactoryDependencies dependencies;
  dependencies.network_thread = c->network_thread_.get();
  dependencies.worker_thread = c->worker_thread_.get();
  dependencies.signaling_thread = c->signaling_thread_.get();
  dependencies.task_queue_factory = webrtc::CreateDefaultTaskQueueFactory();
  dependencies.event_log_factory =
      absl::make_unique<webrtc::RtcEventLogFactory>(
          dependencies.task_queue_factory.get());

  void* jni_env = sora::GetJNIEnv();

  dependencies.adm = c->worker_thread_->BlockingCall([&] {
    sora::AudioDeviceModuleConfig config;
    if (!c->config_.use_audio_device) {
      config.audio_layer = webrtc::AudioDeviceModule::kDummyAudio;
    }
    config.task_queue_factory = dependencies.task_queue_factory.get();
    config.jni_env = sora::GetJNIEnv();
    if (c->config_.get_android_application_context) {
      config.application_context =
          c->config_.get_android_application_context(config.jni_env);
    }
    return sora::CreateAudioDeviceModule(config);
  });

  dependencies.audio_encoder_factory =
      webrtc::CreateBuiltinAudioEncoderFactory();
  dependencies.audio_decoder_factory =
      webrtc::CreateBuiltinAudioDecoderFactory();

  std::shared_ptr<sora::CudaContext> cuda_context;
  if (c->config_.use_hardware_encoder) {
    cuda_context = sora::CudaContext::Create();
  }

  {
    auto config = c->config_.use_hardware_encoder
                      ? sora::GetDefaultVideoEncoderFactoryConfig(
                            cuda_context, jni_env, c->config_.openh264)
                      : sora::GetSoftwareOnlyVideoEncoderFactoryConfig(
                            c->config_.openh264);
    config.use_simulcast_adapter = true;
    config.force_i420_conversion_for_simulcast_adapter =
        c->config_.force_i420_conversion_for_simulcast_adapter;
    dependencies.video_encoder_factory =
        absl::make_unique<sora::SoraVideoEncoderFactory>(std::move(config));
  }
  {
    // auto env = webrtc::CreateEnvironment();
    auto config = c->config_.use_hardware_encoder
                      ? sora::GetDefaultVideoDecoderFactoryConfig(
                            env, cuda_context, jni_env)
                      : sora::GetSoftwareOnlyVideoDecoderFactoryConfig(env);
    dependencies.video_decoder_factory =
        absl::make_unique<sora::SoraVideoDecoderFactory>(std::move(config));
  }

  dependencies.audio_mixer = nullptr;
  dependencies.audio_processing = webrtc::AudioProcessingBuilder().Create();

  if (c->config_.configure_dependencies) {
    c->config_.configure_dependencies(dependencies);
  }

  webrtc::EnableMedia(dependencies);

  c->factory_ = sora::CreateModularPeerConnectionFactoryWithContext(
      std::move(dependencies), c->connection_context_);

  if (c->factory_ == nullptr) {
    RTC_LOG(LS_ERROR) << "Failed to create PeerConnectionFactory";
    return nullptr;
  }

  webrtc::PeerConnectionFactoryInterface::Options factory_options;
  factory_options.disable_encryption = false;
  factory_options.ssl_max_version = rtc::SSL_PROTOCOL_DTLS_12;
  factory_options.crypto_options.srtp.enable_gcm_crypto_suites = true;
  c->factory_->SetOptions(factory_options);

  return c;
}

}  // namespace sora