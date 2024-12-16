// Sora
#include <sora/camera_device_capturer.h>
#include <sora/sora_client_context.h>
#include <sora/srtp_keying_material_exporter.h>

#include <fstream>
#include <optional>
#include <regex>
#include <sstream>

// CLI11
#include <CLI/CLI.hpp>

// WebRTC
#include <rtc_base/crypto_random.h>

#include "sdl_renderer.h"

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

  std::string srtp_key_log_file;

  bool insecure = false;
  std::string client_cert;
  std::string client_key;
  std::string ca_cert;

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
      renderer_.reset(new SDLRenderer(
          config_.window_width, config_.window_height, config_.fullscreen));
    }

    auto size = config_.GetSize();
    if (config_.role != "recvonly") {
      sora::CameraDeviceCapturerConfig cam_config;
      cam_config.width = size.width;
      cam_config.height = size.height;
      cam_config.fps = 30;
      cam_config.use_native = config_.hw_mjpeg_decoder;
      auto video_source = sora::CreateCameraDeviceCapturer(cam_config);
      if (video_source == nullptr) {
        RTC_LOG(LS_ERROR) << "Failed to create video source.";
        return;
      }

      std::string audio_track_id = rtc::CreateRandomString(16);
      std::string video_track_id = rtc::CreateRandomString(16);
      audio_track_ = context_->peer_connection_factory()->CreateAudioTrack(
          audio_track_id, context_->peer_connection_factory()
                              ->CreateAudioSource(cricket::AudioOptions())
                              .get());
      video_track_ = context_->peer_connection_factory()->CreateVideoTrack(
          video_source, video_track_id);
      if (config_.use_sdl && config_.show_me) {
        renderer_->AddTrack(video_track_.get());
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
    conn_ = sora::SoraSignaling::Create(config);

    boost::asio::executor_work_guard<boost::asio::io_context::executor_type>
        work_guard(ioc_->get_executor());

    boost::asio::signal_set signals(*ioc_, SIGINT, SIGTERM);
    signals.async_wait(
        [this](const boost::system::error_code&, int) { conn_->Disconnect(); });

    conn_->Connect();

    if (config_.use_sdl) {
      renderer_->SetDispatchFunction([this](std::function<void()> f) {
        if (ioc_->stopped())
          return;
        boost::asio::dispatch(ioc_->get_executor(), f);
      });
    }

    ioc_->run();
  }

  void OnSetOffer(std::string offer) override {
    std::string stream_id = rtc::CreateRandomString(16);
    if (audio_track_ != nullptr) {
      webrtc::RTCErrorOr<rtc::scoped_refptr<webrtc::RtpSenderInterface>>
          audio_result =
              conn_->GetPeerConnection()->AddTrack(audio_track_, {stream_id});
    }
    if (video_track_ != nullptr) {
      webrtc::RTCErrorOr<rtc::scoped_refptr<webrtc::RtpSenderInterface>>
          video_result =
              conn_->GetPeerConnection()->AddTrack(video_track_, {stream_id});
    }
  }
  void OnDisconnect(sora::SoraSignalingErrorCode ec,
                    std::string message) override {
    RTC_LOG(LS_INFO) << "OnDisconnect: " << message;
    renderer_.reset();
    ioc_->stop();
  }
  void OnNotify(std::string text) override {
    auto json = boost::json::parse(text);
    if (json.at("event_type").as_string() == "connection.created") {
      if (!config_.srtp_key_log_file.empty() && !key_exported_) {
        auto km = sora::ExportKeyingMaterial(conn_->GetPeerConnection(),
                                             conn_->GetVideoMid());
        if (!km) {
          RTC_LOG(LS_ERROR) << "Failed to ExportKeyingMaterial";
          return;
        }
        auto to_hex = [](const std::vector<uint8_t>& buf) {
          std::string str;
          const char hex[] = "0123456789abcdef";
          for (auto n : buf) {
            str += hex[(n >> 3) & 0xf];
            str += hex[n & 0xe];
          }
          return str;
        };
        std::stringstream ss;
        ss << "SRTP_CLIENT_KEY " << to_hex(km->client_write_key) << "\n";
        ss << "SRTP_CLIENT_SALT " << to_hex(km->client_write_salt) << "\n";
        ss << "SRTP_SERVER_KEY " << to_hex(km->server_write_key) << "\n";
        ss << "SRTP_SERVER_SALT " << to_hex(km->server_write_salt) << "\n";
        std::ofstream ofs(config_.srtp_key_log_file, std::ios::app);
        if (ofs) {
          ofs.write(ss.str().c_str(), ss.str().size());
        }
        key_exported_ = true;
      }
    }
  }
  void OnPush(std::string text) override {}
  void OnMessage(std::string label, std::string data) override {}

  void OnTrack(rtc::scoped_refptr<webrtc::RtpTransceiverInterface> transceiver)
      override {
    if (renderer_ == nullptr) {
      return;
    }
    auto track = transceiver->receiver()->track();
    if (track->kind() == webrtc::MediaStreamTrackInterface::kVideoKind) {
      renderer_->AddTrack(
          static_cast<webrtc::VideoTrackInterface*>(track.get()));
    }
  }
  void OnRemoveTrack(
      rtc::scoped_refptr<webrtc::RtpReceiverInterface> receiver) override {
    if (renderer_ == nullptr) {
      return;
    }
    auto track = receiver->track();
    if (track->kind() == webrtc::MediaStreamTrackInterface::kVideoKind) {
      renderer_->RemoveTrack(
          static_cast<webrtc::VideoTrackInterface*>(track.get()));
    }
  }

  void OnDataChannel(std::string label) override {}

 private:
  std::shared_ptr<sora::SoraClientContext> context_;
  SumomoConfig config_;
  rtc::scoped_refptr<webrtc::AudioTrackInterface> audio_track_;
  rtc::scoped_refptr<webrtc::VideoTrackInterface> video_track_;
  std::shared_ptr<sora::SoraSignaling> conn_;
  std::unique_ptr<boost::asio::io_context> ioc_;
  std::unique_ptr<SDLRenderer> renderer_;
  bool key_exported_ = false;
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

  int log_level = (int)rtc::LS_ERROR;
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

  // SRTP keying material の出力
  app.add_option("--srtp-key-log-file", config.srtp_key_log_file,
                 "SRTP keying material output file");

  // 証明書に関するオプション
  app.add_flag("--insecure", config.insecure, "Allow insecure connection");
  app.add_option("--client-cert", config.client_cert, "Client certificate file")->check(CLI::ExistingFile);
  app.add_option("--client-key", config.client_key, "Client key file")->check(CLI::ExistingFile);
  app.add_option("--ca-cert", config.ca_cert, "CA certificate file")->check(CLI::ExistingFile);

  // SoraClientContextConfig に関するオプション
  std::optional<bool> use_hardware_encoder;
  add_optional_bool(app, "--use-hardware-encoder", use_hardware_encoder,
                    "Use hardware encoder");
  std::string openh264;
  app.add_option("--openh264", openh264, "Path to OpenH264 library");

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

  if (log_level != rtc::LS_NONE) {
    rtc::LogMessage::LogToDebug((rtc::LoggingSeverity)log_level);
    rtc::LogMessage::LogTimestamps();
    rtc::LogMessage::LogThreads();
  }

  auto context_config = sora::SoraClientContextConfig();
  if (use_hardware_encoder != std::nullopt) {
    context_config.use_hardware_encoder = *use_hardware_encoder;
  }
  context_config.openh264 = openh264;
  auto context = sora::SoraClientContext::Create(context_config);
  auto sumomo = std::make_shared<Sumomo>(context, config);
  sumomo->Run();

  return 0;
}
