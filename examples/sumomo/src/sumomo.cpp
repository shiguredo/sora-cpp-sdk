// Sora
#include <sora/camera_device_capturer.h>
#include <sora/sora_client_context.h>
#include <sora/sora_video_codec.h>

#include <fstream>
#include <optional>
#include <regex>
#include <sstream>

// CLI11
#include <CLI/CLI.hpp>

// WebRTC
#include <rtc_base/crypto_random.h>

#include "ansi_renderer.h"
#include "sdl_renderer.h"
#include "sixel_renderer.h"

#ifdef _WIN32
#include <rtc_base/win/scoped_com_initializer.h>
#endif

struct SumomoConfig {
  std::string signaling_url;
  std::string channel_id;
  std::string role;
  std::string client_id;
  bool video = true;
  bool audio = true;
  std::string video_device;
  std::string video_codec_type;
  std::string audio_codec_type;
  std::string resolution = "VGA";
  bool hw_mjpeg_decoder = false;
  int video_bit_rate = 0;
  int audio_bit_rate = 0;
  boost::json::value video_h264_params;
  boost::json::value video_h265_params;
  boost::json::value metadata;
  std::optional<bool> multistream;
  std::optional<bool> spotlight;
  int spotlight_number = 0;
  std::optional<bool> simulcast;
  std::optional<bool> data_channel_signaling;
  std::optional<bool> ignore_disconnect_websocket;

  std::string proxy_url;
  std::string proxy_username;
  std::string proxy_password;

  bool use_sdl = false;
  int window_width = 640;
  int window_height = 480;
  bool show_me = false;
  bool fullscreen = false;

  bool use_sixel = false;
  int sixel_width = 640;
  int sixel_height = 480;

  bool use_ansi = false;
  int ansi_width = 80;
  int ansi_height = 40;

  bool insecure = false;
  std::string client_cert;
  std::string client_key;
  std::string ca_cert;

  std::optional<webrtc::DegradationPreference> degradation_preference;

  struct Size {
    int width;
    int height;
  };

  Size GetSize() {
    if (resolution == "QVGA") {
      return {320, 240};
    } else if (resolution == "VGA") {
      return {640, 480};
    } else if (resolution == "HD") {
      return {1280, 720};
    } else if (resolution == "FHD") {
      return {1920, 1080};
    } else if (resolution == "4K") {
      return {3840, 2160};
    }

    // 数字で指定した場合の処理 (例 640x480 )
    auto pos = resolution.find('x');
    if (pos == std::string::npos) {
      return {16, 16};
    }
    auto width = std::atoi(resolution.substr(0, pos).c_str());
    auto height = std::atoi(resolution.substr(pos + 1).c_str());
    return {std::max(16, width), std::max(16, height)};
  }
};

class Sumomo : public std::enable_shared_from_this<Sumomo>,
               public sora::SoraSignalingObserver {
 public:
  Sumomo(std::shared_ptr<sora::SoraClientContext> context, SumomoConfig config)
      : context_(context), config_(config) {}

  void Run() {
    if (config_.use_sdl) {
      sdl_renderer_.reset(new SDLRenderer(
          config_.window_width, config_.window_height, config_.fullscreen));
    }

    if (config_.use_sixel) {
      sixel_renderer_.reset(
          new SixelRenderer(config_.sixel_width, config_.sixel_height));
    }

    if (config_.use_ansi) {
      ansi_renderer_.reset(
          new AnsiRenderer(config_.ansi_width, config_.ansi_height));
    }

    auto size = config_.GetSize();
    if (config_.role != "recvonly") {
      sora::CameraDeviceCapturerConfig cam_config;
      cam_config.width = size.width;
      cam_config.height = size.height;
      cam_config.fps = 30;
      cam_config.use_native = config_.hw_mjpeg_decoder;
      cam_config.device_name = config_.video_device;
      auto video_source = sora::CreateCameraDeviceCapturer(cam_config);
      if (video_source == nullptr) {
        RTC_LOG(LS_ERROR) << "Failed to create video source.";
        return;
      }

      std::string audio_track_id = webrtc::CreateRandomString(16);
      std::string video_track_id = webrtc::CreateRandomString(16);
      audio_track_ = context_->peer_connection_factory()->CreateAudioTrack(
          audio_track_id, context_->peer_connection_factory()
                              ->CreateAudioSource(webrtc::AudioOptions())
                              .get());
      video_track_ = context_->peer_connection_factory()->CreateVideoTrack(
          video_source, video_track_id);
      if (config_.use_sdl && config_.show_me) {
        sdl_renderer_->AddTrack(video_track_.get());
      }
      if (config_.use_sixel && config_.show_me) {
        sixel_renderer_->AddTrack(video_track_.get());
      }
      if (config_.use_ansi && config_.show_me) {
        ansi_renderer_->AddTrack(video_track_.get());
      }
    }

    ioc_.reset(new boost::asio::io_context(1));

    sora::SoraSignalingConfig config;
    config.pc_factory = context_->peer_connection_factory();
    config.io_context = ioc_.get();
    config.observer = shared_from_this();
    config.signaling_urls.push_back(config_.signaling_url);
    config.channel_id = config_.channel_id;
    config.role = config_.role;
    config.multistream = config_.multistream;
    config.client_id = config_.client_id;
    config.video = config_.video;
    config.audio = config_.audio;
    config.video_codec_type = config_.video_codec_type;
    config.audio_codec_type = config_.audio_codec_type;
    config.video_bit_rate = config_.video_bit_rate;
    config.audio_bit_rate = config_.audio_bit_rate;
    config.video_h264_params = config_.video_h264_params;
    config.video_h265_params = config_.video_h265_params;
    config.metadata = config_.metadata;
    config.multistream = config_.multistream;
    config.spotlight = config_.spotlight;
    config.spotlight_number = config_.spotlight_number;
    config.simulcast = config_.simulcast;
    config.data_channel_signaling = config_.data_channel_signaling;
    config.ignore_disconnect_websocket = config_.ignore_disconnect_websocket;
    config.proxy_agent = "Momo Sample for Sora C++ SDK";
    config.proxy_url = config_.proxy_url;
    config.proxy_username = config_.proxy_username;
    config.proxy_password = config_.proxy_password;
    config.network_manager =
        context_->signaling_thread()->BlockingCall([this]() {
          return context_->connection_context()->default_network_manager();
        });
    config.socket_factory =
        context_->signaling_thread()->BlockingCall([this]() {
          return context_->connection_context()->default_socket_factory();
        });
    config.insecure = config_.insecure;
    auto load_file = [](const std::string& path) {
      std::ifstream ifs(path, std::ios::binary);
      if (!ifs) {
        return std::string();
      }
      return std::string((std::istreambuf_iterator<char>(ifs)),
                         std::istreambuf_iterator<char>());
    };
    if (!config_.client_cert.empty()) {
      config.client_cert = load_file(config_.client_cert);
    }
    if (!config_.client_key.empty()) {
      config.client_key = load_file(config_.client_key);
    }
    if (!config_.ca_cert.empty()) {
      config.ca_cert = load_file(config_.ca_cert);
    }
    config.degradation_preference = config_.degradation_preference;
    conn_ = sora::SoraSignaling::Create(config);

    boost::asio::executor_work_guard<boost::asio::io_context::executor_type>
        work_guard(ioc_->get_executor());

    boost::asio::signal_set signals(*ioc_, SIGINT, SIGTERM);
    signals.async_wait(
        [this](const boost::system::error_code&, int) { conn_->Disconnect(); });

    conn_->Connect();

    if (config_.use_sdl) {
      sdl_renderer_->SetDispatchFunction([this](std::function<void()> f) {
        if (ioc_->stopped())
          return;
        boost::asio::dispatch(ioc_->get_executor(), f);
      });
    }

    ioc_->run();
  }

  void OnSetOffer(std::string offer) override {
    std::string stream_id = webrtc::CreateRandomString(16);
    if (audio_track_ != nullptr) {
      webrtc::RTCErrorOr<webrtc::scoped_refptr<webrtc::RtpSenderInterface>>
          audio_result =
              conn_->GetPeerConnection()->AddTrack(audio_track_, {stream_id});
    }
    if (video_track_ != nullptr) {
      webrtc::RTCErrorOr<webrtc::scoped_refptr<webrtc::RtpSenderInterface>>
          video_result =
              conn_->GetPeerConnection()->AddTrack(video_track_, {stream_id});
    }
  }
  void OnDisconnect(sora::SoraSignalingErrorCode ec,
                    std::string message) override {
    RTC_LOG(LS_INFO) << "OnDisconnect: " << message;
    sdl_renderer_.reset();
    sixel_renderer_.reset();
    ansi_renderer_.reset();
    ioc_->stop();
  }
  void OnNotify(std::string text) override {
    RTC_LOG(LS_INFO) << "OnNotify: " << text;
  }
  void OnPush(std::string text) override {}
  void OnMessage(std::string label, std::string data) override {}

  void OnTrack(webrtc::scoped_refptr<webrtc::RtpTransceiverInterface>
                   transceiver) override {
    auto track = transceiver->receiver()->track();
    if (track->kind() == webrtc::MediaStreamTrackInterface::kVideoKind) {
      if (sdl_renderer_) {
        sdl_renderer_->AddTrack(
            static_cast<webrtc::VideoTrackInterface*>(track.get()));
      }
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
  void OnRemoveTrack(
      webrtc::scoped_refptr<webrtc::RtpReceiverInterface> receiver) override {
    auto track = receiver->track();
    if (track->kind() == webrtc::MediaStreamTrackInterface::kVideoKind) {
      if (sdl_renderer_) {
        sdl_renderer_->RemoveTrack(
            static_cast<webrtc::VideoTrackInterface*>(track.get()));
      }
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

  void OnDataChannel(std::string label) override {}

 private:
  std::shared_ptr<sora::SoraClientContext> context_;
  SumomoConfig config_;
  webrtc::scoped_refptr<webrtc::AudioTrackInterface> audio_track_;
  webrtc::scoped_refptr<webrtc::VideoTrackInterface> video_track_;
  std::shared_ptr<sora::SoraSignaling> conn_;
  std::unique_ptr<boost::asio::io_context> ioc_;
  std::unique_ptr<SDLRenderer> sdl_renderer_;
  std::unique_ptr<SixelRenderer> sixel_renderer_;
  std::unique_ptr<AnsiRenderer> ansi_renderer_;
};

void add_optional_bool(CLI::App& app,
                       const std::string& option_name,
                       std::optional<bool>& v,
                       const std::string& help_text) {
  auto f = [&v](const std::string& input) {
    if (input == "true") {
      v = true;
    } else if (input == "false") {
      v = false;
    } else if (input == "none") {
      v = std::nullopt;
    } else {
      throw CLI::ConversionError(input, "optional<bool>");
    }
  };
  app.add_option_function<std::string>(option_name, f, help_text)
      ->type_name("TEXT")
      ->check(CLI::IsMember({"true", "false", "none"}));
}

int main(int argc, char* argv[]) {
#ifdef _WIN32
  webrtc::ScopedCOMInitializer com_initializer(
      webrtc::ScopedCOMInitializer::kMTA);
  if (!com_initializer.Succeeded()) {
    std::cerr << "CoInitializeEx failed" << std::endl;
    return 1;
  }
#endif

  SumomoConfig config;

  auto is_valid_resolution = CLI::Validator(
      [](std::string input) -> std::string {
        if (input == "QVGA" || input == "VGA" || input == "HD" ||
            input == "FHD" || input == "4K") {
          return std::string();
        }

        // 数値x数値、というフォーマットになっているか確認する
        std::regex re("^[1-9][0-9]*x[1-9][0-9]*$");
        if (std::regex_match(input, re)) {
          return std::string();
        }

        return "Must be one of QVGA, VGA, HD, FHD, 4K, or "
               "[WIDTH]x[HEIGHT].";
      },
      "");

  auto is_json = CLI::Validator(
      [](std::string input) -> std::string {
        boost::system::error_code ec;
        boost::json::parse(input, ec);
        if (ec) {
          return "Value " + input + " is not JSON Value";
        }
        return std::string();
      },
      "JSON Value");

  CLI::App app("Momo Sample for Sora C++ SDK");

  int log_level = (int)webrtc::LS_ERROR;
  auto log_level_map = std::vector<std::pair<std::string, int>>(
      {{"verbose", 0}, {"info", 1}, {"warning", 2}, {"error", 3}, {"none", 4}});
  app.add_option("--log-level", log_level, "Log severity level threshold")
      ->transform(CLI::CheckedTransformer(log_level_map, CLI::ignore_case));
  app.add_option("--resolution", config.resolution,
                 "Video resolution (one of QVGA, VGA, HD, FHD, 4K, or "
                 "[WIDTH]x[HEIGHT])")
      ->check(is_valid_resolution);
  app.add_option("--hw-mjpeg-decoder", config.hw_mjpeg_decoder,
                 "Perform MJPEG deoode and video resize by hardware "
                 "acceleration only on supported devices (default: false)");

  // Sora に関するオプション
  app.add_option("--signaling-url", config.signaling_url, "Signaling URL")
      ->required();
  app.add_option("--channel-id", config.channel_id, "Channel ID")->required();
  app.add_option("--role", config.role, "Role")
      ->check(CLI::IsMember({"sendonly", "recvonly", "sendrecv"}))
      ->required();
  app.add_option("--client-id", config.client_id, "Client ID");
  app.add_option("--video", config.video, "Send video to sora (default: true)");
  app.add_option("--audio", config.audio, "Send audio to sora (default: true)");
  app.add_option("--video-device", config.video_device, "Video device name");
  app.add_option("--video-codec-type", config.video_codec_type,
                 "Video codec for send")
      ->check(CLI::IsMember({"", "VP8", "VP9", "AV1", "H264", "H265"}));
  app.add_option("--audio-codec-type", config.audio_codec_type,
                 "Audio codec for send")
      ->check(CLI::IsMember({"", "OPUS"}));
  app.add_option("--video-bit-rate", config.video_bit_rate, "Video bit rate")
      ->check(CLI::Range(0, 30000));
  app.add_option("--audio-bit-rate", config.audio_bit_rate, "Audio bit rate")
      ->check(CLI::Range(0, 510));
  std::string video_h264_params;
  app.add_option("--video-h264-params", video_h264_params,
                 "Parameters for H.264 video codec");
  std::string video_h265_params;
  app.add_option("--video-h265-params", video_h265_params,
                 "Parameters for H.265 video codec");
  std::string metadata;
  app.add_option("--metadata", metadata,
                 "Signaling metadata used in connect message")
      ->check(is_json);
  add_optional_bool(app, "--multistream", config.multistream,
                    "Use multistream (default: none)");
  add_optional_bool(app, "--spotlight", config.spotlight,
                    "Use spotlight (default: none)");
  app.add_option("--spotlight-number", config.spotlight_number,
                 "Stream count delivered in spotlight")
      ->check(CLI::Range(0, 8));
  add_optional_bool(app, "--simulcast", config.simulcast,
                    "Use simulcast (default: none)");
  add_optional_bool(app, "--data-channel-signaling",
                    config.data_channel_signaling,
                    "Use DataChannel for Sora signaling (default: none)");
  add_optional_bool(
      app, "--ignore-disconnect-websocket", config.ignore_disconnect_websocket,
      "Ignore WebSocket disconnection if using Data Channel (default: none)");

  // proxy の設定
  app.add_option("--proxy-url", config.proxy_url, "Proxy URL");
  app.add_option("--proxy-username", config.proxy_username, "Proxy username");
  app.add_option("--proxy-password", config.proxy_password, "Proxy password");

  // SDL に関するオプション
  app.add_flag("--use-sdl", config.use_sdl, "Show video using SDL");
  app.add_option("--window-width", config.window_width, "SDL window width");
  app.add_option("--window-height", config.window_height, "SDL window height");
  app.add_flag("--fullscreen", config.fullscreen,
               "Use fullscreen window for videos");
  app.add_flag("--show-me", config.show_me, "Show self video");

  // Sixel に関するオプション
  app.add_flag("--use-sixel", config.use_sixel, "Show video using Sixel");
  app.add_option("--sixel-width", config.sixel_width, "Sixel output width");
  app.add_option("--sixel-height", config.sixel_height, "Sixel output height");

  // ANSI に関するオプション
  app.add_flag("--use-ansi", config.use_ansi,
               "Show video using ANSI escape sequences");
  app.add_option("--ansi-width", config.ansi_width,
                 "ANSI output width (in characters)");
  app.add_option("--ansi-height", config.ansi_height,
                 "ANSI output height (in lines)");

  // 証明書に関するオプション
  app.add_flag("--insecure", config.insecure, "Allow insecure connection");
  app.add_option("--client-cert", config.client_cert, "Client certificate file")
      ->check(CLI::ExistingFile);
  app.add_option("--client-key", config.client_key, "Client key file")
      ->check(CLI::ExistingFile);
  app.add_option("--ca-cert", config.ca_cert, "CA certificate file")
      ->check(CLI::ExistingFile);

  // DegradationPreference に関するオプション
  auto degradation_preference_map =
      std::vector<std::pair<std::string, webrtc::DegradationPreference>>(
          {{"disabled", webrtc::DegradationPreference::DISABLED},
           {"maintain_framerate",
            webrtc::DegradationPreference::MAINTAIN_FRAMERATE},
           {"maintain_resolution",
            webrtc::DegradationPreference::MAINTAIN_RESOLUTION},
           {"balanced", webrtc::DegradationPreference::BALANCED}});
  app.add_option("--degradation-preference", config.degradation_preference,
                 "Degradation preference")
      ->transform(CLI::CheckedTransformer(degradation_preference_map,
                                          CLI::ignore_case));

  // SoraClientContextConfig に関するオプション
  std::string audio_recording_device;
  app.add_option("--audio-recording-device", audio_recording_device,
                 "Recording device name");
  std::string audio_playout_device;
  app.add_option("--audio-playout-device", audio_playout_device,
                 "Playout device name");
  std::string openh264;
  app.add_option("--openh264", openh264, "Path to OpenH264 library");
  auto video_codec_implementation_map =
      std::vector<std::pair<std::string, sora::VideoCodecImplementation>>(
          {{"internal", sora::VideoCodecImplementation::kInternal},
           {"cisco_openh264", sora::VideoCodecImplementation::kCiscoOpenH264},
           {"intel_vpl", sora::VideoCodecImplementation::kIntelVpl},
           {"nvidia_video_codec_sdk",
            sora::VideoCodecImplementation::kNvidiaVideoCodecSdk},
           {"amd_amf", sora::VideoCodecImplementation::kAmdAmf}});
  auto video_codec_description =
      "value in "
      "{internal,cisco_openh264,intel_vpl,nvidia_video_codec_sdk,amd_amf}";
  std::optional<sora::VideoCodecImplementation> vp8_encoder;
  std::optional<sora::VideoCodecImplementation> vp8_decoder;
  std::optional<sora::VideoCodecImplementation> vp9_encoder;
  std::optional<sora::VideoCodecImplementation> vp9_decoder;
  std::optional<sora::VideoCodecImplementation> h264_encoder;
  std::optional<sora::VideoCodecImplementation> h264_decoder;
  std::optional<sora::VideoCodecImplementation> h265_encoder;
  std::optional<sora::VideoCodecImplementation> h265_decoder;
  std::optional<sora::VideoCodecImplementation> av1_encoder;
  std::optional<sora::VideoCodecImplementation> av1_decoder;
  app.add_option("--vp8-encoder", vp8_encoder, "VP8 encoder implementation")
      ->transform(CLI::CheckedTransformer(video_codec_implementation_map,
                                          CLI::ignore_case)
                      .description(video_codec_description));
  app.add_option("--vp8-decoder", vp8_decoder, "VP8 decoder implementation")
      ->transform(CLI::CheckedTransformer(video_codec_implementation_map,
                                          CLI::ignore_case)
                      .description(video_codec_description));
  app.add_option("--vp9-encoder", vp9_encoder, "VP9 encoder implementation")
      ->transform(CLI::CheckedTransformer(video_codec_implementation_map,
                                          CLI::ignore_case)
                      .description(video_codec_description));
  app.add_option("--vp9-decoder", vp9_decoder, "VP9 decoder implementation")
      ->transform(CLI::CheckedTransformer(video_codec_implementation_map,
                                          CLI::ignore_case)
                      .description(video_codec_description));
  app.add_option("--h264-encoder", h264_encoder, "H.264 encoder implementation")
      ->transform(CLI::CheckedTransformer(video_codec_implementation_map,
                                          CLI::ignore_case)
                      .description(video_codec_description));
  app.add_option("--h264-decoder", h264_decoder, "H.264 decoder implementation")
      ->transform(CLI::CheckedTransformer(video_codec_implementation_map,
                                          CLI::ignore_case)
                      .description(video_codec_description));
  app.add_option("--h265-encoder", h265_encoder, "H.265 encoder implementation")
      ->transform(CLI::CheckedTransformer(video_codec_implementation_map,
                                          CLI::ignore_case)
                      .description(video_codec_description));
  app.add_option("--h265-decoder", h265_decoder, "H.265 decoder implementation")
      ->transform(CLI::CheckedTransformer(video_codec_implementation_map,
                                          CLI::ignore_case)
                      .description(video_codec_description));
  app.add_option("--av1-encoder", av1_encoder, "AV1 encoder implementation")
      ->transform(CLI::CheckedTransformer(video_codec_implementation_map,
                                          CLI::ignore_case)
                      .description(video_codec_description));
  app.add_option("--av1-decoder", av1_decoder, "AV1 decoder implementation")
      ->transform(CLI::CheckedTransformer(video_codec_implementation_map,
                                          CLI::ignore_case)
                      .description(video_codec_description));
  bool show_video_codec_capability = false;
  app.add_flag("--show-video-codec-capability", show_video_codec_capability,
               "Show video codec capability");

  // 表示して終了する系の処理のために、まず必須のオプションをオフにしておく
  std::vector<CLI::Option*> required_options;
  for (const auto& option : app.get_options()) {
    if (option->get_required()) {
      required_options.push_back(option);
      option->required(false);
    }
  }

  try {
    app.parse(argc, argv);
  } catch (const CLI::ParseError& e) {
    exit(app.exit(e));
  }

  if (log_level != webrtc::LS_NONE) {
    webrtc::LogMessage::LogToDebug((webrtc::LoggingSeverity)log_level);
    webrtc::LogMessage::LogTimestamps();
    webrtc::LogMessage::LogThreads();
  }

  // 表示して終了する系の処理はここに書く
  if (show_video_codec_capability) {
    sora::VideoCodecCapabilityConfig config;
    if (sora::CudaContext::CanCreate()) {
      config.cuda_context = sora::CudaContext::Create();
    }
    if (sora::AMFContext::CanCreate()) {
      config.amf_context = sora::AMFContext::Create();
    }
    config.openh264_path = openh264;
    auto capability = sora::GetVideoCodecCapability(config);
    for (const auto& engine : capability.engines) {
      std::cout << "Engine: "
                << boost::json::value_from(engine.name).as_string()
                << std::endl;
      for (const auto& codec : engine.codecs) {
        if (codec.encoder) {
          std::cout << "  - " << boost::json::value_from(codec.type).as_string()
                    << " Encoder" << std::endl;
        }
        if (codec.decoder) {
          std::cout << "  - " << boost::json::value_from(codec.type).as_string()
                    << " Decoder" << std::endl;
        }
        auto params = boost::json::value_from(codec.parameters);
        if (params.as_object().size() > 0) {
          std::cout << "    - Codec Parameters: "
                    << boost::json::serialize(params) << std::endl;
        }
      }
      auto params = boost::json::value_from(engine.parameters);
      if (params.as_object().size() > 0) {
        std::cout << "  - Engine Parameters: " << boost::json::serialize(params)
                  << std::endl;
      }
    }
    return 0;
  }

  // 必須のオプションを元に戻して再度パースする
  for (const auto& option : required_options) {
    option->required(true);
  }
  try {
    app.parse(argc, argv);
  } catch (const CLI::ParseError& e) {
    exit(app.exit(e));
  }

  if (!video_h264_params.empty()) {
    config.video_h264_params = boost::json::parse(video_h264_params);
  }

  if (!video_h265_params.empty()) {
    config.video_h265_params = boost::json::parse(video_h265_params);
  }

  // メタデータのパース
  if (!metadata.empty()) {
    config.metadata = boost::json::parse(metadata);
  }

  auto context_config = sora::SoraClientContextConfig();
  if (!audio_recording_device.empty()) {
    context_config.audio_recording_device = audio_recording_device;
  }
  if (!audio_playout_device.empty()) {
    context_config.audio_playout_device = audio_playout_device;
  }
  context_config.use_audio_device = false;
  context_config.video_codec_factory_config.preference = std::invoke([&]() {
    std::optional<sora::VideoCodecPreference> preference;
    auto add_codec_preference =
        [&preference](webrtc::VideoCodecType type,
                      std::optional<sora::VideoCodecImplementation> encoder,
                      std::optional<sora::VideoCodecImplementation> decoder) {
          if (encoder || decoder) {
            if (!preference) {
              preference = sora::VideoCodecPreference();
            }
            auto& codec = preference->GetOrAdd(type);
            codec.encoder = encoder;
            codec.decoder = decoder;
          }
        };
    add_codec_preference(webrtc::kVideoCodecVP8, vp8_encoder, vp8_decoder);
    add_codec_preference(webrtc::kVideoCodecVP9, vp9_encoder, vp9_decoder);
    add_codec_preference(webrtc::kVideoCodecH264, h264_encoder, h264_decoder);
    add_codec_preference(webrtc::kVideoCodecH265, h265_encoder, h265_decoder);
    add_codec_preference(webrtc::kVideoCodecAV1, av1_encoder, av1_decoder);
    return preference;
  });
  // 指定されてる VideoCodecImplementation に必要なコンテキストのみ設定する
  if (context_config.video_codec_factory_config.preference) {
    if (context_config.video_codec_factory_config.preference->HasImplementation(
            sora::VideoCodecImplementation::kCiscoOpenH264)) {
      context_config.video_codec_factory_config.capability_config
          .openh264_path = openh264;
    }
    if (context_config.video_codec_factory_config.preference->HasImplementation(
            sora::VideoCodecImplementation::kNvidiaVideoCodecSdk)) {
      if (sora::CudaContext::CanCreate()) {
        context_config.video_codec_factory_config.capability_config
            .cuda_context = sora::CudaContext::Create();
      }
    }
    if (context_config.video_codec_factory_config.preference->HasImplementation(
            sora::VideoCodecImplementation::kAmdAmf)) {
      if (sora::AMFContext::CanCreate()) {
        context_config.video_codec_factory_config.capability_config
            .amf_context = sora::AMFContext::Create();
      }
    }
  }

  auto context = sora::SoraClientContext::Create(context_config);
  auto sumomo = std::make_shared<Sumomo>(context, config);
  sumomo->Run();

  return 0;
}
