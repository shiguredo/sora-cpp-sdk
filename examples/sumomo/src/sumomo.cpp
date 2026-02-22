#include <algorithm>
#include <csignal>
#include <cstdlib>
#include <exception>
#include <fstream>
#include <functional>
#include <ios>
#include <iostream>
#include <iterator>
#include <memory>
#include <optional>
#include <ostream>
#include <regex>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

// Boost
#include <boost/asio/dispatch.hpp>
#include <boost/asio/error.hpp>
#include <boost/asio/executor_work_guard.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/address.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/signal_set.hpp>
#include <boost/asio/socket_base.hpp>
#include <boost/beast/core/error.hpp>
#include <boost/beast/core/flat_buffer.hpp>
#include <boost/beast/http/field.hpp>
#include <boost/beast/http/impl/read.hpp>
#include <boost/beast/http/message_fwd.hpp>
#include <boost/beast/http/status.hpp>
#include <boost/beast/http/string_body_fwd.hpp>
#include <boost/beast/http/verb.hpp>
#include <boost/beast/version.hpp>
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
#include <api/stats/rtc_stats_report.h>
#include <api/video/video_codec_type.h>
#include <rtc_base/crypto_random.h>
#include <rtc_base/logging.h>

#ifdef _WIN32
#include <rtc_base/win/scoped_com_initializer.h>
#endif

// Sora C++ SDK
#include <sora/amf_context.h>
#include <sora/boost_json_iwyu.h>
#include <sora/camera_device_capturer.h>
#include <sora/cuda_context.h>
#include <sora/renderer/ansi_renderer.h>
#include <sora/renderer/sixel_renderer.h>
#include <sora/rtc_stats.h>
#include <sora/sora_client_context.h>
#include <sora/sora_signaling.h>
#include <sora/sora_video_codec.h>

// CLI11
#include <CLI/CLI.hpp>

#include "sdl_renderer.h"

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
  std::optional<bool> cpu_adaptation;

  std::optional<int> http_port;
  std::string http_host = "127.0.0.1";

  bool use_libcamera = false;
  bool use_libcamera_native = false;
  std::vector<std::pair<std::string, std::string>> libcamera_controls;

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

    // 数字で指定した場合の処理 (例 640x480)
    auto pos = resolution.find('x');
    if (pos == std::string::npos) {
      // TODO: 無効な形式の場合、16x16 を返すよりエラーを投げるべきか検討
      // 現在は最小解像度として 16x16 を返している
      return {16, 16};
    }
    auto width = std::atoi(resolution.substr(0, pos).c_str());
    auto height = std::atoi(resolution.substr(pos + 1).c_str());
    return {std::max(16, width), std::max(16, height)};
  }
};

namespace beast = boost::beast;
namespace http = beast::http;
namespace net = boost::asio;
using tcp = net::ip::tcp;

class Sumomo;

// HTTP リクエストを処理するセッション
//
// 現在は1リクエスト=1コネクションの設計。
// Keep-Alive に対応する場合、リクエストを処理し終わった後に
// 再度 DoRead() を呼ぶ必要がある。
class HttpSession : public std::enable_shared_from_this<HttpSession> {
 public:
  HttpSession(tcp::socket socket, std::weak_ptr<Sumomo> sumomo)
      : socket_(std::move(socket)), sumomo_(sumomo) {}

  void Run() { DoRead(); }

 private:
  void DoRead() {
    request_ = {};

    http::async_read(
        socket_, buffer_, request_,
        [self = shared_from_this()](beast::error_code ec, std::size_t) {
          if (!ec) {
            self->HandleRequest();
          }
        });
  }

  void HandleRequest() {
    if (request_.method() == http::verb::get && request_.target() == "/stats") {
      auto sumomo = sumomo_.lock();
      if (!sumomo) {
        SendErrorResponse(http::status::service_unavailable,
                          "Service unavailable");
        return;
      }

      GetStatsFromSumomo(sumomo);
    } else {
      SendErrorResponse(http::status::not_found, "Not found");
    }
  }

  void SendErrorResponse(http::status status, const std::string& message) {
    http::response<http::string_body> res{status, request_.version()};
    res.set(http::field::server, BOOST_BEAST_VERSION_STRING);
    res.set(http::field::content_type, "text/plain");
    res.keep_alive(request_.keep_alive());
    res.body() = message;
    res.prepare_payload();
    Send(std::move(res));
  }

  template <typename Response>
  void Send(Response&& res) {
    auto sp = std::make_shared<Response>(std::forward<Response>(res));
    http::async_write(
        socket_, *sp,
        [self = shared_from_this(), sp](beast::error_code ec, std::size_t) {
          // TODO: 現在はエラーハンドリングをしていないが、
          // エラー時のログ出力とソケットの明示的なクローズを検討する
          // 現状は Keep-Alive 非対応で1リクエスト=1コネクションなので
          // 大きな問題にはなっていない
          self->socket_.shutdown(tcp::socket::shutdown_send, ec);
        });
  }

  // この関数内で Sumomo のメンバ関数を呼び出すため、
  // Sumomo クラスの完全な定義が必要となる。
  // そのため、この関数の実装は Sumomo クラスの定義後に記述している。
  // TODO: 将来的にはより良い設計（例: stats 取得用のインターフェース）を検討
  void GetStatsFromSumomo(std::shared_ptr<Sumomo> sumomo);

  tcp::socket socket_;
  std::weak_ptr<Sumomo> sumomo_;
  beast::flat_buffer buffer_;
  http::request<http::string_body> request_;
};

class HttpListener : public std::enable_shared_from_this<HttpListener> {
 public:
  HttpListener(net::io_context& ioc,
               tcp::endpoint endpoint,
               std::weak_ptr<Sumomo> sumomo)
      : ioc_(ioc), acceptor_(ioc), sumomo_(sumomo) {
    beast::error_code ec;

    acceptor_.open(endpoint.protocol(), ec);
    if (ec) {
      throw std::runtime_error("Failed to open acceptor: " + ec.message());
    }

    acceptor_.set_option(net::socket_base::reuse_address(true), ec);
    if (ec) {
      throw std::runtime_error("Failed to set reuse_address: " + ec.message());
    }

    acceptor_.bind(endpoint, ec);
    if (ec) {
      throw std::runtime_error("Failed to bind: " + ec.message());
    }

    acceptor_.listen(net::socket_base::max_listen_connections, ec);
    if (ec) {
      throw std::runtime_error("Failed to listen: " + ec.message());
    }
  }

  ~HttpListener() { Stop(); }

 public:
  void Run() { DoAccept(); }

  void Stop() {
    beast::error_code ec;
    acceptor_.close(ec);
    if (ec) {
      RTC_LOG(LS_ERROR) << "Failed to close acceptor: " << ec.message();
    }
  }

 private:
  void DoAccept() {
    acceptor_.async_accept([self = shared_from_this()](beast::error_code ec,
                                                       tcp::socket socket) {
      if (ec) {
        // Stop() 時の acceptor_.close() により operation_aborted が発生するが、
        // これは正常な終了なのでエラーログは出力しない
        if (ec != net::error::operation_aborted) {
          RTC_LOG(LS_ERROR) << "Accept failed: " << ec.message();
        }
        return;
      }
      std::make_shared<HttpSession>(std::move(socket), self->sumomo_)->Run();
      self->DoAccept();
    });
  }

  net::io_context& ioc_;
  tcp::acceptor acceptor_;
  std::weak_ptr<Sumomo> sumomo_;
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
          new sora::SixelRenderer(config_.sixel_width, config_.sixel_height));
    }

    if (config_.use_ansi) {
      ansi_renderer_.reset(
          new sora::AnsiRenderer(config_.ansi_width, config_.ansi_height));
    }

    auto size = config_.GetSize();
    if (config_.role != "recvonly") {
      sora::CameraDeviceCapturerConfig cam_config;
      cam_config.width = size.width;
      cam_config.height = size.height;
      cam_config.fps = 30;
      cam_config.use_native = config_.hw_mjpeg_decoder;
      cam_config.device_name = config_.video_device;
      cam_config.use_libcamera = config_.use_libcamera;
      cam_config.libcamera_native_frame_output = config_.use_libcamera_native;
      cam_config.libcamera_controls = config_.libcamera_controls;
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
    config.spotlight = config_.spotlight;
    config.spotlight_number = config_.spotlight_number;
    config.simulcast = config_.simulcast;
    config.data_channel_signaling = config_.data_channel_signaling;
    config.ignore_disconnect_websocket = config_.ignore_disconnect_websocket;
    config.proxy_agent = "Sumomo Sample for Sora C++ SDK";
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
    config.cpu_adaptation = config_.cpu_adaptation;
    conn_ = sora::SoraSignaling::Create(config);

    boost::asio::executor_work_guard<boost::asio::io_context::executor_type>
        work_guard(ioc_->get_executor());

    // HTTP サーバーの起動（接続前に起動する）
    if (config_.http_port.has_value()) {
      try {
        // TODO: make_address は無効なアドレスで例外を投げるが、
        // より具体的なエラーメッセージのためにエラーコード版の使用を検討
        tcp::endpoint endpoint{boost::asio::ip::make_address(config_.http_host),
                               static_cast<unsigned short>(*config_.http_port)};
        http_listener_ =
            std::make_shared<HttpListener>(*ioc_, endpoint, weak_from_this());
        http_listener_->Run();
        RTC_LOG(LS_INFO) << "HTTP server listening on " << config_.http_host
                         << ":" << *config_.http_port;
      } catch (const std::exception& e) {
        RTC_LOG(LS_ERROR) << "Failed to start HTTP server: " << e.what();
        return;
      }
    }

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
    http_listener_.reset();
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

  void GetStats(std::function<void(const std::string&)> callback) {
    if (!conn_ || !conn_->GetPeerConnection()) {
      callback("[]");
      return;
    }

    conn_->GetPeerConnection()->GetStats(
        sora::RTCStatsCallback::Create(
            [callback = std::move(callback)](
                const webrtc::scoped_refptr<const webrtc::RTCStatsReport>&
                    report) { callback(report->ToJson()); })
            .get());
  }

 private:
  std::shared_ptr<sora::SoraClientContext> context_;
  SumomoConfig config_;
  webrtc::scoped_refptr<webrtc::AudioTrackInterface> audio_track_;
  webrtc::scoped_refptr<webrtc::VideoTrackInterface> video_track_;
  std::shared_ptr<sora::SoraSignaling> conn_;
  std::unique_ptr<boost::asio::io_context> ioc_;
  std::unique_ptr<SDLRenderer> sdl_renderer_;
  std::unique_ptr<sora::SixelRenderer> sixel_renderer_;
  std::unique_ptr<sora::AnsiRenderer> ansi_renderer_;
  std::shared_ptr<HttpListener> http_listener_;
};

void HttpSession::GetStatsFromSumomo(std::shared_ptr<Sumomo> sumomo) {
  sumomo->GetStats([self = shared_from_this()](const std::string& stats) {
    http::response<http::string_body> res{http::status::ok,
                                          self->request_.version()};
    res.set(http::field::server, BOOST_BEAST_VERSION_STRING);
    res.set(http::field::content_type, "application/json");
    res.keep_alive(self->request_.keep_alive());
    res.body() = stats;
    res.prepare_payload();
    self->Send(std::move(res));
  });
}

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

  CLI::App app("Sumomo Sample for Sora C++ SDK");

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
                 "Perform MJPEG decode and video resize by hardware "
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

  // ANSI エスケープシーケンスに関するオプション
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

  // HTTP サーバーに関するオプション
  app.add_option("--http-port", config.http_port,
                 "HTTP server port for stats API")
      ->check(CLI::Range(1024, 65535));
  app.add_option("--http-host", config.http_host,
                 "HTTP server host address (default: 127.0.0.1)");

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

  add_optional_bool(app, "--cpu-adaptation", config.cpu_adaptation,
                    "Enable/disable CPU adaptation (default: none)");

  app.add_flag("--use-libcamera", config.use_libcamera,
               "Use libcamera for video capture (only on supported devices)");
  app.add_flag("--use-libcamera-native", config.use_libcamera_native,
               "Use native buffer for H.264 encoding");
  app.add_option("--libcamera-control", config.libcamera_controls,
                 "Set libcamera control (format: key value)");

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
           {"nvidia_video_codec",
            sora::VideoCodecImplementation::kNvidiaVideoCodec},
           {"amd_amf", sora::VideoCodecImplementation::kAmdAmf},
           {"raspi_v4l2m2m", sora::VideoCodecImplementation::kRaspiV4L2M2M}});
  auto video_codec_description =
      "value in "
      "{internal,cisco_openh264,intel_vpl,nvidia_video_codec,amd_amf,raspi_"
      "v4l2m2m}";
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
            sora::VideoCodecImplementation::kNvidiaVideoCodec)) {
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

  // V4L2 用のネイティブバッファを使う場合、I420 変換は無効化する
  // V4L2NativeBuffer は ToI420() をサポートしていないため
  if (config.use_libcamera_native) {
    context_config.video_codec_factory_config.encoder_factory_config
        .force_i420_conversion = false;
  }

  auto context = sora::SoraClientContext::Create(context_config);
  auto sumomo = std::make_shared<Sumomo>(context, config);
  sumomo->Run();

  return 0;
}
