#include "hello.h"

#include <csignal>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

// Boost
#include <boost/asio/executor_work_guard.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/signal_set.hpp>
#include <boost/system/detail/error_code.hpp>

// WebRTC
#include <api/audio_options.h>
#include <api/media_stream_interface.h>
#include <api/rtc_error.h>
#include <api/rtp_parameters.h>
#include <api/rtp_receiver_interface.h>
#include <api/rtp_sender_interface.h>
#include <api/rtp_transceiver_interface.h>
#include <api/scoped_refptr.h>
#include <rtc_base/crypto_random.h>
#include <rtc_base/logging.h>
#include <sora/renderer/ansi_renderer.h>
#include <sora/renderer/sixel_renderer.h>

#ifdef _WIN32
#include <rtc_base/win/scoped_com_initializer.h>
#endif

// Sora C++ SDK
#include <sora/amf_context.h>
#include <sora/boost_json_iwyu.h>
#include <sora/capturer/fake_video_capturer.h>
#include <sora/cuda_context.h>
#include <sora/java_context.h>
#include <sora/sora_client_context.h>
#include <sora/sora_signaling.h>
#include <sora/sora_video_codec.h>

#if defined(HELLO_ANDROID)
void* GetAndroidApplicationContext(void* env);
#else
void* GetAndroidApplicationContext(void* env) {
  return nullptr;
}
#endif

HelloSora::HelloSora(std::shared_ptr<sora::SoraClientContext> context,
                     HelloSoraConfig config)
    : context_(context), config_(config) {}

HelloSora::~HelloSora() {
  RTC_LOG(LS_INFO) << "HelloSora dtor";
  ioc_.reset();
  video_track_ = nullptr;
  audio_track_ = nullptr;
  video_source_ = nullptr;
}

void HelloSora::Run() {
  void* env = sora::GetJNIEnv();

  if (config_.use_sixel) {
    sixel_renderer_.reset(
        new sora::SixelRenderer(config_.sixel_width, config_.sixel_height));
  }

  if (config_.use_ansi) {
    ansi_renderer_.reset(
        new sora::AnsiRenderer(config_.ansi_width, config_.ansi_height));
  }

  sora::FakeVideoCapturerConfig fake_config;
  fake_config.width = config_.capture_width;
  fake_config.height = config_.capture_height;
  fake_config.fps = 30;
  video_source_ = sora::FakeVideoCapturer::Create(fake_config);
  std::string video_track_id = webrtc::CreateRandomString(16);
  video_track_ = pc_factory()->CreateVideoTrack(video_source_, video_track_id);

  std::string audio_track_id = webrtc::CreateRandomString(16);
  audio_track_ = pc_factory()->CreateAudioTrack(
      audio_track_id,
      pc_factory()->CreateAudioSource(webrtc::AudioOptions()).get());

  ioc_.reset(new boost::asio::io_context(1));

  sora::SoraSignalingConfig config;
  config.pc_factory = pc_factory();
  config.io_context = ioc_.get();
  config.observer = shared_from_this();
  config.signaling_urls = config_.signaling_urls;
  config.channel_id = config_.channel_id;
  config.role = config_.role;
  config.video = config_.video;
  config.audio = config_.audio;
  config.video_codec_type = config_.video_codec_type;
  config.video_bit_rate = config_.video_bit_rate;
  config.multistream = true;
  config.simulcast = config_.simulcast;
  config.data_channel_signaling = config_.data_channel_signaling;
  if (config_.ignore_disconnect_websocket) {
    config.ignore_disconnect_websocket = *config_.ignore_disconnect_websocket;
  }
  if (!config_.client_id.empty()) {
    config.client_id = config_.client_id;
  }
  if (!config_.data_channels.empty()) {
    config.data_channels = config_.data_channels;
  }
  if (!config_.forwarding_filters.empty()) {
    config.forwarding_filters = config_.forwarding_filters;
  }
  config.degradation_preference = config_.degradation_preference;
  conn_ = sora::SoraSignaling::Create(config);

  boost::asio::executor_work_guard<boost::asio::io_context::executor_type>
      work_guard(ioc_->get_executor());

  boost::asio::signal_set signals(*ioc_, SIGINT, SIGTERM);
  signals.async_wait(
      [this](const boost::system::error_code&, int) { conn_->Disconnect(); });

  conn_->Connect();
  ioc_->run();
}

void HelloSora::OnSetOffer(std::string offer) {
  std::string stream_id = webrtc::CreateRandomString(16);
  webrtc::RTCErrorOr<webrtc::scoped_refptr<webrtc::RtpSenderInterface>>
      audio_result =
          conn_->GetPeerConnection()->AddTrack(audio_track_, {stream_id});
  if (video_track_ != nullptr) {
    webrtc::RTCErrorOr<webrtc::scoped_refptr<webrtc::RtpSenderInterface>>
        video_result =
            conn_->GetPeerConnection()->AddTrack(video_track_, {stream_id});
  }
}
void HelloSora::OnDisconnect(sora::SoraSignalingErrorCode ec,
                             std::string message) {
  RTC_LOG(LS_INFO) << "OnDisconnect: " << message;
  ioc_->stop();
}

void HelloSora::OnTrack(
    webrtc::scoped_refptr<webrtc::RtpTransceiverInterface> transceiver) {
  auto track = transceiver->receiver()->track();
  if (track->kind() == webrtc::MediaStreamTrackInterface::kVideoKind) {
    if (sixel_renderer_) {
      sixel_renderer_->AddTrack(
          static_cast<webrtc::VideoTrackInterface*>(track.get()));
    }
    if (ansi_renderer_) {
      ansi_renderer_->AddTrack(
          static_cast<webrtc::VideoTrackInterface*>(track.get()));
    }
  }
}

void HelloSora::OnRemoveTrack(
    webrtc::scoped_refptr<webrtc::RtpReceiverInterface> receiver) {
  auto track = receiver->track();
  if (track->kind() == webrtc::MediaStreamTrackInterface::kVideoKind) {
    if (sixel_renderer_) {
      sixel_renderer_->RemoveTrack(
          static_cast<webrtc::VideoTrackInterface*>(track.get()));
    }
    if (ansi_renderer_) {
      ansi_renderer_->RemoveTrack(
          static_cast<webrtc::VideoTrackInterface*>(track.get()));
    }
  }
}

#if defined(__ANDROID__) || (defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE)

// iOS, Android は main を使わない

#else

int main(int argc, char* argv[]) {
  if (argc < 2) {
    std::cout << argv[0] << " <param.json>" << std::endl;
    return -1;
  }

#ifdef _WIN32
  webrtc::ScopedCOMInitializer com_initializer(
      webrtc::ScopedCOMInitializer::kMTA);
  if (!com_initializer.Succeeded()) {
    std::cerr << "CoInitializeEx failed" << std::endl;
    return 1;
  }
#endif

  webrtc::LogMessage::LogToDebug(webrtc::LS_WARNING);
  webrtc::LogMessage::LogTimestamps();
  webrtc::LogMessage::LogThreads();

  boost::json::value v;
  {
    std::ifstream ifs(argv[1]);
    std::ostringstream oss;
    oss << ifs.rdbuf();
    std::string js = oss.str();
    boost::json::parse_options opt;
    opt.allow_comments = true;
    opt.allow_trailing_commas = true;
    v = boost::json::parse(js, {}, opt);
  }

  HelloSoraConfig config;
  for (auto&& x : v.as_object().at("signaling_urls").as_array()) {
    config.signaling_urls.push_back(x.as_string().c_str());
  }
  config.channel_id = v.as_object().at("channel_id").as_string().c_str();
  boost::json::value x;
  auto get = [](const boost::json::value& v, const char* key,
                boost::json::value& x) -> bool {
    if (auto it = v.as_object().find(key);
        it != v.as_object().end() && !it->value().is_null()) {
      x = it->value();
      return true;
    }
    return false;
  };
  if (get(v, "role", x)) {
    config.role = x.as_string();
  }
  if (get(v, "video", x)) {
    config.video = x.as_bool();
  }
  if (get(v, "audio", x)) {
    config.audio = x.as_bool();
  }
  if (get(v, "capture_width", x)) {
    config.capture_width = x.to_number<int>();
  }
  if (get(v, "capture_height", x)) {
    config.capture_height = x.to_number<int>();
  }
  if (get(v, "video_bit_rate", x)) {
    config.video_bit_rate = x.to_number<int>();
  }
  if (get(v, "video_codec_type", x)) {
    config.video_codec_type = x.as_string();
  }
  if (get(v, "simulcast", x)) {
    config.simulcast = x.as_bool();
  }
  if (get(v, "client_id", x)) {
    config.client_id = x.as_string();
  }
  if (get(v, "data_channel_signaling", x)) {
    config.data_channel_signaling = x.as_bool();
  }
  if (get(v, "ignore_disconnect_websocket", x)) {
    config.ignore_disconnect_websocket = x.as_bool();
  }
  if (get(v, "data_channels", x)) {
    for (auto&& dc : x.as_array()) {
      sora::SoraSignalingConfig::DataChannel data_channel;
      data_channel.label = dc.as_object().at("label").as_string();
      data_channel.direction = dc.as_object().at("direction").as_string();
      boost::json::value y;
      if (get(dc, "ordered", y)) {
        data_channel.ordered = y.as_bool();
      }
      if (get(dc, "max_packet_life_time", y)) {
        data_channel.max_packet_life_time = y.to_number<int32_t>();
      }
      if (get(dc, "max_retransmits", y)) {
        data_channel.max_retransmits = y.to_number<int32_t>();
      }
      if (get(dc, "protocol", y)) {
        data_channel.protocol = y.as_string().c_str();
      }
      if (get(dc, "compress", y)) {
        data_channel.compress = y.as_bool();
      }
      if (get(dc, "header", y)) {
        data_channel.header.emplace(y.as_array().begin(), y.as_array().end());
      }
      config.data_channels.push_back(data_channel);
    }
  }
  if (get(v, "forwarding_filters", x)) {
    for (auto&& ff : x.as_array()) {
      sora::SoraSignalingConfig::ForwardingFilter forwarding_filter;
      boost::json::value y;
      if (get(ff, "name", y)) {
        forwarding_filter.name.emplace(y.as_string());
      }
      if (get(ff, "priority", y)) {
        forwarding_filter.priority.emplace(y.to_number<int>());
      }
      if (get(ff, "action", y)) {
        forwarding_filter.action.emplace(y.as_string());
      }
      for (auto&& rs : ff.as_object().at("rules").as_array()) {
        std::vector<sora::SoraSignalingConfig::ForwardingFilter::Rule> rules;
        for (auto&& r : rs.as_array()) {
          sora::SoraSignalingConfig::ForwardingFilter::Rule rule;
          rule.field = r.as_object().at("field").as_string();
          rule.op = r.as_object().at("operator").as_string();
          for (auto&& v : r.as_object().at("values").as_array()) {
            rule.values.push_back(v.as_string().c_str());
          }
          rules.push_back(rule);
        }
        forwarding_filter.rules.push_back(rules);
      }
      if (get(ff, "version", y)) {
        forwarding_filter.version.emplace(y.as_string());
      }
      if (get(ff, "metadata", y)) {
        forwarding_filter.metadata = y;
      }
      config.forwarding_filters.push_back(forwarding_filter);
    }
  }
  if (get(v, "log_level", x)) {
    webrtc::LogMessage::LogToDebug((webrtc::LoggingSeverity)x.to_number<int>());
  }
  if (get(v, "degradation_preference", x)) {
    if (x.as_string() == "disabled") {
      config.degradation_preference = webrtc::DegradationPreference::DISABLED;
    } else if (x.as_string() == "maintain_framerate") {
      config.degradation_preference =
          webrtc::DegradationPreference::MAINTAIN_FRAMERATE;
    } else if (x.as_string() == "maintain_resolution") {
      config.degradation_preference =
          webrtc::DegradationPreference::MAINTAIN_RESOLUTION;
    } else if (x.as_string() == "balanced") {
      config.degradation_preference = webrtc::DegradationPreference::BALANCED;
    }
  }
  if (get(v, "use_sixel", x)) {
    config.use_sixel = x.as_bool();
  }
  if (get(v, "sixel_width", x)) {
    config.sixel_width = x.to_number<int>();
  }
  if (get(v, "sixel_height", x)) {
    config.sixel_height = x.to_number<int>();
  }
  if (get(v, "use_ansi", x)) {
    config.use_ansi = x.as_bool();
  }
  if (get(v, "ansi_width", x)) {
    config.ansi_width = x.to_number<int>();
  }
  if (get(v, "ansi_height", x)) {
    config.ansi_height = x.to_number<int>();
  }

  sora::SoraClientContextConfig context_config;
  context_config.get_android_application_context = GetAndroidApplicationContext;
  if (get(v, "use_audio_device", x)) {
    context_config.use_audio_device = x.as_bool();
  }
  if (get(v, "openh264", x)) {
    context_config.video_codec_factory_config.capability_config.openh264_path =
        x.as_string();
    auto& preference =
        context_config.video_codec_factory_config.preference.emplace();
    auto capability = sora::GetVideoCodecCapability(
        context_config.video_codec_factory_config.capability_config);
    preference.Merge(sora::CreateVideoCodecPreferenceFromImplementation(
        capability, sora::VideoCodecImplementation::kInternal));
    preference.Merge(sora::CreateVideoCodecPreferenceFromImplementation(
        capability, sora::VideoCodecImplementation::kCiscoOpenH264));
  }
  if (get(v, "video_codec_preference", x)) {
    auto& preference =
        context_config.video_codec_factory_config.preference.emplace();
    preference.codecs =
        boost::json::value_to<std::vector<sora::VideoCodecPreference::Codec>>(
            x);

    if (preference.HasImplementation(
            sora::VideoCodecImplementation::kNvidiaVideoCodec)) {
      if (sora::CudaContext::CanCreate()) {
        context_config.video_codec_factory_config.capability_config
            .cuda_context = sora::CudaContext::Create();
      }
    }
    if (preference.HasImplementation(sora::VideoCodecImplementation::kAmdAmf)) {
      if (sora::AMFContext::CanCreate()) {
        context_config.video_codec_factory_config.capability_config
            .amf_context = sora::AMFContext::Create();
      }
    }
  }

  auto context = sora::SoraClientContext::Create(context_config);

  auto hello = std::make_shared<HelloSora>(context, config);
  hello->Run();
}

#endif
