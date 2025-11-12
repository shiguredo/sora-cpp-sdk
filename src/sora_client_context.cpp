#include "sora/sora_client_context.h"

#include <memory>
#include <optional>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

// WebRTC
#include <absl/memory/memory.h>
#include <api/audio/audio_device.h>
#include <api/audio/audio_device_defines.h>
#include <api/audio/builtin_audio_processing_builder.h>
#include <api/audio_codecs/builtin_audio_decoder_factory.h>
#include <api/audio_codecs/builtin_audio_encoder_factory.h>
#include <api/enable_media.h>
#include <api/environment/environment_factory.h>
#include <api/peer_connection_interface.h>
#include <api/rtc_event_log/rtc_event_log_factory.h>
#include <pc/media_factory.h>
#include <rtc_base/logging.h>
#include <rtc_base/ssl_adapter.h>
#include <rtc_base/ssl_stream_adapter.h>
#include <rtc_base/thread.h>

#include "sora/audio_device_module.h"
#include "sora/java_context.h"
#include "sora/sora_peer_connection_factory.h"
#include "sora/sora_video_codec_factory.h"

namespace sora {

SoraClientContext::~SoraClientContext() {
  config_ = SoraClientContextConfig();
  connection_context_ = nullptr;
  factory_ = nullptr;
  network_thread_->Stop();
  worker_thread_->Stop();
  signaling_thread_->Stop();

  //webrtc::CleanupSSL();
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
  auto env = webrtc::CreateEnvironment();
  dependencies.network_thread = c->network_thread_.get();
  dependencies.worker_thread = c->worker_thread_.get();
  dependencies.signaling_thread = c->signaling_thread_.get();
  dependencies.event_log_factory =
      absl::make_unique<webrtc::RtcEventLogFactory>(&env.task_queue_factory());

  auto adm = c->worker_thread_->BlockingCall([&] {
    sora::AudioDeviceModuleConfig config;
    if (!c->config_.use_audio_device) {
      config.audio_layer = webrtc::AudioDeviceModule::kDummyAudio;
    }
    config.env = env;
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
    // ADM が差し替えられた可能性があるので再取得する
    c->worker_thread_->BlockingCall([&] { adm = dependencies.adm; });
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
  c->factory_->SetOptions(factory_options);

#if defined(SORA_CPP_SDK_ANDROID)
  // Android はデバイスの数が１個として返される上に、
  // RecordingDeviceName() や PlayoutDeviceName() を呼び出すとクラッシュする実装になっているので
  // オーディオデバイスの列挙と設定を行わない。
  // ref: https://source.chromium.org/chromium/chromium/src/+/main:third_party/webrtc/sdk/android/src/jni/audio_device/audio_device_module.cc;l=145-161;drc=d4937d3336bcf86f2fb3363cb6a64a0eb1a36576
#else
  auto success = c->worker_thread_->BlockingCall([&]() -> bool {
    // オーディオデバイス名を列挙する
    auto get_audio_devices = [adm](bool is_recording) {
      std::vector<std::tuple<std::string, std::string> > devices;
      int device_count =
          is_recording ? adm->RecordingDevices() : adm->PlayoutDevices();
      // RecordingDevices, PlayoutDevice がマイナスの値を返すことがある
      if (device_count >= 0) {
        devices.resize(device_count);
      }
      for (int i = 0; i < device_count; i++) {
        char name[webrtc::kAdmMaxDeviceNameSize];
        char guid[webrtc::kAdmMaxGuidSize];
        int err;
        err = is_recording ? adm->SetRecordingDevice(i)
                           : adm->SetPlayoutDevice(i);
        if (err != 0) {
          RTC_LOG(LS_WARNING)
              << "Failed to "
              << (is_recording ? "SetRecordingDevice" : "SetPlayoutDevice")
              << ": index=" << i;
          continue;
        }
        bool available = false;
        err = is_recording ? adm->RecordingIsAvailable(&available)
                           : adm->PlayoutIsAvailable(&available);
        if (err != 0) {
          RTC_LOG(LS_WARNING)
              << "Failed to "
              << (is_recording ? "RecordingIsAvailable" : "PlayoutIsAvailable")
              << ": index=" << i;
          continue;
        }

        if (!available) {
          continue;
        }
        err = is_recording ? adm->RecordingDeviceName(i, name, guid)
                           : adm->PlayoutDeviceName(i, name, guid);
        if (err != 0) {
          RTC_LOG(LS_WARNING) << "Failed to RecordingDeviceName: index=" << i;
          continue;
        }
        RTC_LOG(LS_INFO) << (is_recording ? "RecordingDeviceName"
                                          : "PlayoutDeviceName")
                         << ": index=" << i << " name=" << name
                         << " guid=" << guid;
        std::get<0>(devices[i]) = name;
        std::get<1>(devices[i]) = guid;
      }
      if (device_count >= 2) {
        adm->SetRecordingDevice(0);
      }
      return devices;
    };
    std::vector<std::tuple<std::string, std::string> > recording_devices =
        get_audio_devices(true);
    std::vector<std::tuple<std::string, std::string> > playout_devices =
        get_audio_devices(false);

    auto set_audio_device = [adm](std::optional<std::string> device_name,
                                  const std::vector<
                                      std::tuple<std::string, std::string> >&
                                      devices,
                                  bool is_recording) {
      if (!device_name) {
        // デバイス名が指定されていない場合はデフォルトデバイスを使う
        // 明示的に 0 を指定しないと、Windows の場合は -1（無効なデバイス）が使われてしまう
        if (!devices.empty()) {
          is_recording ? adm->SetRecordingDevice(0) : adm->SetPlayoutDevice(0);
        }
        return true;
      }
      int index = -1;
      for (int i = 0; i < devices.size(); i++) {
        const auto& name = std::get<0>(devices[i]);
        const auto& guid = std::get<1>(devices[i]);
        if (*device_name == name || *device_name == guid) {
          index = i;
          break;
        }
      }
      if (index == -1) {
        RTC_LOG(LS_ERROR) << "No " << (is_recording ? "recording" : "playout")
                          << " device found: name=" << *device_name;
        return false;
      }

      const auto& name = std::get<0>(devices[index]);
      const auto& guid = std::get<1>(devices[index]);
      int err = is_recording ? adm->SetRecordingDevice(index)
                             : adm->SetPlayoutDevice(index);
      if (err == 0) {
        RTC_LOG(LS_INFO) << "Succeeded "
                         << (is_recording ? "SetRecordingDevice"
                                          : "SetPlayoutDevice")
                         << ": index=" << index << " name=" << name
                         << " guid=" << guid;
      } else {
        RTC_LOG(LS_ERROR) << "Failed to "
                          << (is_recording ? "SetRecordingDevice"
                                           : "SetPlayoutDevice")
                          << ": index=" << index << " name=" << name
                          << " guid=" << guid;
        return false;
      }
    };
    if (!set_audio_device(c->config_.audio_recording_device, recording_devices,
                          true)) {
      return false;
    }
    if (!set_audio_device(c->config_.audio_playout_device, playout_devices,
                          false)) {
      return false;
    }
    return true;
  });
  if (!success) {
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
