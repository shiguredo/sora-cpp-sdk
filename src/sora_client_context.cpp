#include "sora/sora_client_context.h"

// WebRTC
#include <api/audio/builtin_audio_processing_builder.h>
#include <api/audio_codecs/builtin_audio_decoder_factory.h>
#include <api/audio_codecs/builtin_audio_encoder_factory.h>
#include <api/create_peerconnection_factory.h>
#include <api/enable_media.h>
#include <api/environment/environment_factory.h>
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
    const SoraClientContextConfig& config) {
  webrtc::InitializeSSL();

  std::shared_ptr<SoraClientContext> c = std::make_shared<SoraClientContext>();

  c->config_ = config;
  c->network_thread_ = webrtc::Thread::CreateWithSocketServer();
  c->network_thread_->Start();
  c->worker_thread_ = webrtc::Thread::Create();
  c->worker_thread_->Start();
  c->signaling_thread_ = webrtc::Thread::Create();
  c->signaling_thread_->Start();

  webrtc::PeerConnectionFactoryDependencies dependencies;
  dependencies.network_thread = c->network_thread_.get();
  dependencies.worker_thread = c->worker_thread_.get();
  dependencies.signaling_thread = c->signaling_thread_.get();
  dependencies.task_queue_factory = webrtc::CreateDefaultTaskQueueFactory();
  dependencies.event_log_factory =
      absl::make_unique<webrtc::RtcEventLogFactory>(
          dependencies.task_queue_factory.get());

  void* env = sora::GetJNIEnv();

  auto adm = c->worker_thread_->BlockingCall([&] {
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
  dependencies.adm = adm;

  dependencies.audio_encoder_factory =
      webrtc::CreateBuiltinAudioEncoderFactory();
  dependencies.audio_decoder_factory =
      webrtc::CreateBuiltinAudioDecoderFactory();

  auto codec_factory =
      CreateVideoCodecFactory(c->config_.video_codec_factory_config);
  if (!codec_factory) {
    RTC_LOG(LS_ERROR) << "Failed to create VideoCodecFactory";
    c->worker_thread_->BlockingCall([&] {
      adm = nullptr;
      dependencies.adm = nullptr;
    });
    return nullptr;
  }
  dependencies.video_encoder_factory =
      std::move(codec_factory->encoder_factory);
  dependencies.video_decoder_factory =
      std::move(codec_factory->decoder_factory);

  dependencies.audio_mixer = nullptr;
  dependencies.audio_processing_builder =
      std::make_unique<webrtc::BuiltinAudioProcessingBuilder>();

  if (c->config_.configure_dependencies) {
    c->config_.configure_dependencies(dependencies);
  }

  webrtc::EnableMedia(dependencies);

  c->factory_ = sora::CreateModularPeerConnectionFactoryWithContext(
      std::move(dependencies), c->connection_context_);

  if (c->factory_ == nullptr) {
    c->worker_thread_->BlockingCall([&] {
      adm = nullptr;
      dependencies.adm = nullptr;
    });
    RTC_LOG(LS_ERROR) << "Failed to create PeerConnectionFactory";
    return nullptr;
  }

  webrtc::PeerConnectionFactoryInterface::Options factory_options;
  factory_options.disable_encryption = false;
  factory_options.ssl_max_version = webrtc::SSL_PROTOCOL_DTLS_12;
  factory_options.crypto_options.srtp.enable_gcm_crypto_suites = true;
  c->factory_->SetOptions(factory_options);

#if defined(SORA_CPP_SDK_ANDROID)
  // Android はデバイスの数が１個として返される上に、
  // RecordingDeviceName() や PlayoutDeviceName() を呼び出すとクラッシュする実装になっているので
  // オーディオデバイスの列挙と設定を行わない。
  // ref: https://source.chromium.org/chromium/chromium/src/+/main:third_party/webrtc/sdk/android/src/jni/audio_device/audio_device_module.cc;l=145-161;drc=d4937d3336bcf86f2fb3363cb6a64a0eb1a36576
#else
  auto r = c->worker_thread_->BlockingCall(
      [&]() -> std::shared_ptr<SoraClientContext> {
        // オーディオデバイス名を列挙して名前を覚える
        std::vector<std::tuple<std::string, std::string> > recording_devices;
        std::vector<std::tuple<std::string, std::string> > playout_devices;
        {
          int recording_device_count = adm->RecordingDevices();
          // RecordingDevices がマイナスの値を返すことがある
          if (recording_device_count >= 0) {
            recording_devices.resize(recording_device_count);
          }
          for (int i = 0; i < recording_device_count; i++) {
            char name[webrtc::kAdmMaxDeviceNameSize];
            char guid[webrtc::kAdmMaxGuidSize];
            if (adm->SetRecordingDevice(i) != 0) {
              RTC_LOG(LS_WARNING)
                  << "Failed to SetRecordingDevice: index=" << i;
              continue;
            }
            bool available = false;
            if (adm->RecordingIsAvailable(&available) != 0) {
              RTC_LOG(LS_WARNING)
                  << "Failed to RecordingIsAvailable: index=" << i;
              continue;
            }

            if (!available) {
              continue;
            }
            if (adm->RecordingDeviceName(i, name, guid) != 0) {
              RTC_LOG(LS_WARNING)
                  << "Failed to RecordingDeviceName: index=" << i;
              continue;
            }
            RTC_LOG(LS_INFO) << "RecordingDevice: index=" << i
                             << " name=" << name << " guid=" << guid;
            std::get<0>(recording_devices[i]) = name;
            std::get<1>(recording_devices[i]) = guid;
          }
          if (recording_device_count >= 2) {
            adm->SetRecordingDevice(0);
          }

          int playout_device_count = adm->PlayoutDevices();
          // PlayoutDevices がマイナスの値を返すことがある
          if (playout_device_count >= 0) {
            playout_devices.resize(playout_device_count);
          }
          for (int i = 0; i < playout_device_count; i++) {
            char name[webrtc::kAdmMaxDeviceNameSize];
            char guid[webrtc::kAdmMaxGuidSize];
            if (adm->SetPlayoutDevice(i) != 0) {
              RTC_LOG(LS_WARNING) << "Failed to SetPlayoutDevice: index=" << i;
              continue;
            }
            bool available = false;
            if (adm->PlayoutIsAvailable(&available) != 0) {
              RTC_LOG(LS_WARNING)
                  << "Failed to PlayoutIsAvailable: index=" << i;
              continue;
            }

            if (!available) {
              continue;
            }
            if (adm->PlayoutDeviceName(i, name, guid) != 0) {
              RTC_LOG(LS_WARNING) << "Failed to PlayoutDeviceName: index=" << i;
              continue;
            }
            RTC_LOG(LS_INFO) << "PlayoutDevice: index=" << i << " name=" << name
                             << " guid=" << guid;
            std::get<0>(playout_devices[i]) = name;
            std::get<1>(playout_devices[i]) = guid;
          }
          if (playout_device_count >= 2) {
            adm->SetPlayoutDevice(0);
          }
        }

        // オーディオデバイスを設定する
        if (c->config_.audio_recording_device) {
          int index = -1;
          for (int i = 0; i < recording_devices.size(); i++) {
            const auto& name = std::get<0>(recording_devices[i]);
            const auto& guid = std::get<1>(recording_devices[i]);
            if (*c->config_.audio_recording_device == name ||
                *c->config_.audio_recording_device == guid) {
              index = i;
              break;
            }
          }
          if (index == -1) {
            RTC_LOG(LS_ERROR) << "No recording device found: name="
                              << *c->config_.audio_recording_device;
            return nullptr;
          }

          const auto& name = std::get<0>(recording_devices[index]);
          const auto& guid = std::get<1>(recording_devices[index]);
          if (adm->SetRecordingDevice(index) == 0) {
            RTC_LOG(LS_INFO) << "Succeeded SetRecordingDevice: index=" << index
                             << " name=" << name << " name=" << guid;
          } else {
            RTC_LOG(LS_ERROR) << "Failed to SetRecordingDevice: index=" << index
                              << " name=" << name << " guid=" << guid;
          }
        }

        if (c->config_.audio_playout_device) {
          int index = -1;
          for (int i = 0; i < playout_devices.size(); i++) {
            const auto& name = std::get<0>(playout_devices[i]);
            const auto& guid = std::get<1>(playout_devices[i]);
            if (*c->config_.audio_playout_device == name ||
                *c->config_.audio_playout_device == guid) {
              index = i;
              break;
            }
          }
          if (index == -1) {
            RTC_LOG(LS_ERROR) << "No playout device found: name="
                              << *c->config_.audio_playout_device;
            return nullptr;
          }

          const auto& name = std::get<0>(playout_devices[index]);
          const auto& guid = std::get<1>(playout_devices[index]);
          if (adm->SetPlayoutDevice(index) == 0) {
            RTC_LOG(LS_INFO) << "Succeeded SetPlayoutDevice: index=" << index
                             << " name=" << name << " guid=" << guid;
          } else {
            RTC_LOG(LS_ERROR) << "Failed to SetPlayoutDevice: index=" << index
                              << " name=" << name << " guid=" << guid;
          }
        }
        return c;
      });
  if (r == nullptr) {
    c->worker_thread_->BlockingCall([&] {
      adm = nullptr;
      dependencies.adm = nullptr;
    });
    return nullptr;
  }
#endif

  return c;
}

}  // namespace sora