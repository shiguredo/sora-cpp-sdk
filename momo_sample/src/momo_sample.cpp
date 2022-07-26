// Sora
#include <sora/camera_device_capturer.h>
#include <sora/sora_default_client.h>

// CLI11
#include <CLI/CLI.hpp>

// Boost
#include <boost/optional/optional_io.hpp>

#include "sdl_renderer.h"

#ifdef _WIN32
#include <rtc_base/win/scoped_com_initializer.h>
#endif

struct MomoSampleConfig : sora::SoraDefaultClientConfig {
  std::string signaling_url;
  std::string channel_id;
  std::string role;
  std::string client_id;
  bool video = true;
  bool audio = true;
  std::string video_codec_type;
  std::string audio_codec_type;
  int video_bit_rate = 0;
  int audio_bit_rate = 0;
  boost::json::value metadata;
  boost::optional<bool> multistream;
  boost::optional<bool> spotlight;
  int spotlight_number = 0;
  boost::optional<bool> simulcast;
  boost::optional<bool> data_channel_signaling;
  boost::optional<bool> ignore_disconnect_websocket;

  std::string proxy_url;
  std::string proxy_username;
  std::string proxy_password;

  bool use_sdl = false;
  int window_width = 640;
  int window_height = 480;
  bool show_me = false;
  bool fullscreen = false;
};

class MomoSample : public std::enable_shared_from_this<MomoSample>,
                   public sora::SoraDefaultClient {
 public:
  MomoSample(MomoSampleConfig config)
      : sora::SoraDefaultClient(config), config_(config) {}

  void Run() {
    if (config_.use_sdl) {
      renderer_.reset(new SDLRenderer(
          config_.window_width, config_.window_height, config_.fullscreen));
    }

    if (config_.role != "recvonly") {
      sora::CameraDeviceCapturerConfig cam_config;
      cam_config.width = 640;
      cam_config.height = 480;
      cam_config.fps = 30;
      auto video_source = sora::CreateCameraDeviceCapturer(cam_config);
      if (video_source == nullptr) {
        RTC_LOG(LS_ERROR) << "Failed to create video source.";
        return;
      }

      std::string audio_track_id = rtc::CreateRandomString(16);
      std::string video_track_id = rtc::CreateRandomString(16);
      audio_track_ = factory()->CreateAudioTrack(
          audio_track_id,
          factory()->CreateAudioSource(cricket::AudioOptions()).get());
      video_track_ =
          factory()->CreateVideoTrack(video_track_id, video_source.get());
      if (config_.use_sdl && config_.show_me) {
        renderer_->AddTrack(video_track_.get());
      }
    }

    ioc_.reset(new boost::asio::io_context(1));

    sora::SoraSignalingConfig config;
    config.pc_factory = factory();
    config.io_context = ioc_.get();
    config.observer = shared_from_this();
    config.signaling_urls.push_back(config_.signaling_url);
    config.channel_id = config_.channel_id;
    config.role = config_.role;
    config.multistream = config_.multistream;
    config.video_codec_type = config_.video_codec_type;
    config.client_id = config_.client_id;
    config.video = config_.video;
    config.audio = config_.audio;
    config.video_codec_type = config_.video_codec_type;
    config.audio_codec_type = config_.audio_codec_type;
    config.video_bit_rate = config_.video_bit_rate;
    config.audio_bit_rate = config_.audio_bit_rate;
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

  void OnSetOffer() override {
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

 private:
  MomoSampleConfig config_;
  rtc::scoped_refptr<webrtc::AudioTrackInterface> audio_track_;
  rtc::scoped_refptr<webrtc::VideoTrackInterface> video_track_;
  std::shared_ptr<sora::SoraSignaling> conn_;
  std::unique_ptr<boost::asio::io_context> ioc_;
  std::unique_ptr<SDLRenderer> renderer_;
};

int main(int argc, char* argv[]) {
#ifdef _WIN32
  webrtc::ScopedCOMInitializer com_initializer(
      webrtc::ScopedCOMInitializer::kMTA);
  if (!com_initializer.Succeeded()) {
    std::cerr << "CoInitializeEx failed" << std::endl;
    return 1;
  }
#endif

  MomoSampleConfig config;

  auto bool_map = std::vector<std::pair<std::string, boost::optional<bool>>>(
      {{"false", false}, {"true", true}});
  auto optional_bool_map =
      std::vector<std::pair<std::string, boost::optional<bool>>>(
          {{"false", false}, {"true", true}, {"none", boost::none}});
  auto is_json = CLI::Validator(
      [](std::string input) -> std::string {
        boost::json::error_code ec;
        boost::json::parse(input);
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
      ->check(CLI::IsMember({"", "VP8", "VP9", "AV1", "H264"}));
  app.add_option("--audio-codec-type", config.audio_codec_type,
                 "Audio codec for send")
      ->check(CLI::IsMember({"", "OPUS"}));
  app.add_option("--video-bit-rate", config.video_bit_rate, "Video bit rate")
      ->check(CLI::Range(0, 30000));
  app.add_option("--audio-bit-rate", config.audio_bit_rate, "Audio bit rate")
      ->check(CLI::Range(0, 510));
  std::string metadata;
  app.add_option("--metadata", metadata,
                 "Signaling metadata used in connect message")
      ->check(is_json);
  app.add_option("--multistream", config.multistream,
                 "Use multistream (default: none)")
      ->transform(CLI::CheckedTransformer(optional_bool_map, CLI::ignore_case));
  app.add_option("--spotlight", config.spotlight, "Use spotlight")
      ->transform(CLI::CheckedTransformer(bool_map, CLI::ignore_case));
  app.add_option("--spotlight-number", config.spotlight_number,
                 "Stream count delivered in spotlight")
      ->check(CLI::Range(0, 8));
  app.add_option("--simulcast", config.simulcast,
                 "Use simulcast (default: false)")
      ->transform(CLI::CheckedTransformer(bool_map, CLI::ignore_case));
  app.add_option("--data-channel-signaling", config.data_channel_signaling,
                 "Use DataChannel for Sora signaling (default: none)")
      ->type_name("TEXT")
      ->transform(CLI::CheckedTransformer(optional_bool_map, CLI::ignore_case));
  app.add_option("--ignore-disconnect-websocket",
                 config.ignore_disconnect_websocket,
                 "Ignore WebSocket disconnection if using Data Channel "
                 "(default: none)")
      ->type_name("TEXT")
      ->transform(CLI::CheckedTransformer(optional_bool_map, CLI::ignore_case));

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

  try {
    app.parse(argc, argv);
  } catch (const CLI::ParseError& e) {
    exit(app.exit(e));
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

  auto momosample = sora::CreateSoraClient<MomoSample>(config);
  momosample->Run();

  return 0;
}
