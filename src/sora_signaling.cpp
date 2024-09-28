#include "sora/sora_signaling.h"

// WebRTC
#include <p2p/client/basic_port_allocator.h>
#include <pc/rtp_media_utils.h>
#include <pc/session_description.h>
#include <rtc_base/crypt_string_revive.h>
#include <rtc_base/proxy_info_revive.h>

#include "sora/data_channel.h"
#include "sora/rtc_ssl_verifier.h"
#include "sora/rtc_stats.h"
#include "sora/session_description.h"
#include "sora/srtp_keying_material_exporter.h"
#include "sora/url_parts.h"
#include "sora/version.h"
#include "sora/zlib_helper.h"

namespace sora {

const char kActionBlock[] = "block";
const char kActionAllow[] = "allow";
const char kFieldConnectionId[] = "connection_id";
const char kFieldClientId[] = "client_id";
const char kFieldKind[] = "kind";
const char kOperatorIsIn[] = "is_in";
const char kOperatorIsNotIn[] = "is_not_in";

SoraSignaling::SoraSignaling(const SoraSignalingConfig& config)
    : config_(config),
      connection_timeout_timer_(*config_.io_context),
      closing_timeout_timer_(*config_.io_context) {}

SoraSignaling::~SoraSignaling() {
  RTC_LOG(LS_INFO) << "SoraSignaling::~SoraSignaling";
}

std::shared_ptr<SoraSignaling> SoraSignaling::Create(
    const SoraSignalingConfig& config) {
  return std::shared_ptr<SoraSignaling>(new SoraSignaling(config));
}

rtc::scoped_refptr<webrtc::PeerConnectionInterface>
SoraSignaling::GetPeerConnection() const {
  return pc_;
}

std::string SoraSignaling::GetVideoMid() const {
  return video_mid_;
}

std::string SoraSignaling::GetAudioMid() const {
  return audio_mid_;
}

std::string SoraSignaling::GetConnectionID() const {
  return connection_id_;
}
std::string SoraSignaling::GetSelectedSignalingURL() const {
  return selected_signaling_url_.load();
}
std::string SoraSignaling::GetConnectedSignalingURL() const {
  return connected_signaling_url_.load();
}
bool SoraSignaling::IsConnectedDataChannel() const {
  return dc_ && using_datachannel_;
}
bool SoraSignaling::IsConnectedWebsocket() const {
  return ws_connected_;
}

void SoraSignaling::Connect() {
  RTC_LOG(LS_INFO) << "SoraSignaling::Connect";

  boost::asio::post(*config_.io_context,
                    [self = shared_from_this()]() { self->DoConnect(); });
}

void SoraSignaling::Disconnect() {
  boost::asio::post(*config_.io_context, [self = shared_from_this()]() {
    if (self->state_ == State::Init) {
      self->state_ = State::Closed;
      return;
    }
    if (self->state_ == State::Connecting) {
      self->SendOnDisconnect(SoraSignalingErrorCode::CLOSE_SUCCEEDED,
                             "Close was called in connecting");
      return;
    }
    if (self->state_ == State::Closing) {
      return;
    }
    if (self->state_ == State::Closed) {
      return;
    }

    self->DoInternalDisconnect(boost::none, "", "");
  });
}

bool SoraSignaling::ParseURL(const std::string& url,
                             URLParts& parts,
                             bool& ssl) {
  if (!URLParts::Parse(url, parts)) {
    return false;
  }

  if (parts.scheme == "wss") {
    ssl = true;
    return true;
  } else if (parts.scheme == "ws") {
    ssl = false;
    return true;
  } else {
    return false;
  }
}

bool SoraSignaling::CheckSdp(const std::string& sdp) {
  return true;
}

void SoraSignaling::Redirect(std::string url) {
  assert(state_ == State::Connected);

  state_ = State::Redirecting;

  ws_->Read([self = shared_from_this(), url](boost::system::error_code ec,
                                             std::size_t bytes_transferred,
                                             std::string text) {
    // リダイレクト中に Disconnect が呼ばれた
    if (self->state_ != State::Redirecting) {
      return;
    }

    auto on_close = [self, url](boost::system::error_code ec) {
      if (!ec) {
        self->SendOnWsClose(boost::beast::websocket::close_reason(
            boost::beast::websocket::close_code::normal));
      }

      if (self->state_ != State::Redirecting) {
        return;
      }

      // close 処理に成功してても失敗してても処理は続ける
      if (ec) {
        RTC_LOG(LS_WARNING) << "Redirect error: ec=" << ec.message();
      }

      // 接続タイムアウト用の処理
      self->connection_timeout_timer_.expires_from_now(
          boost::posix_time::seconds(
              self->config_.websocket_connection_timeout));
      self->connection_timeout_timer_.async_wait(
          [self](boost::system::error_code ec) {
            if (ec) {
              return;
            }

            self->SendOnDisconnect(SoraSignalingErrorCode::INTERNAL_ERROR,
                                   "Connection timeout in redirection");
          });

      URLParts parts;
      bool ssl;
      if (!ParseURL(url, parts, ssl)) {
        self->SendOnDisconnect(SoraSignalingErrorCode::INVALID_PARAMETER,
                               "Invalid URL: url=" + url);
        return;
      }

      std::shared_ptr<Websocket> ws;
      if (ssl) {
        if (self->config_.proxy_url.empty()) {
          ws.reset(
              new Websocket(Websocket::ssl_tag(), *self->config_.io_context,
                            self->config_.insecure, self->config_.client_cert,
                            self->config_.client_key, self->config_.ca_cert));
        } else {
          ws.reset(new Websocket(
              Websocket::https_proxy_tag(), *self->config_.io_context,
              self->config_.insecure, self->config_.client_cert,
              self->config_.client_key, self->config_.ca_cert,
              self->config_.proxy_url, self->config_.proxy_username,
              self->config_.proxy_password));
        }
      } else {
        ws.reset(new Websocket(*self->config_.io_context));
      }
      ws->Connect(url, std::bind(&SoraSignaling::OnRedirect, self,
                                 std::placeholders::_1, url, ws));
    };

    // type: redirect の後、サーバは切断してるはずなので、正常に処理が終わるのはおかしい
    if (!ec) {
      RTC_LOG(LS_WARNING) << "Unexpected success to read";
      // 強制的に閉じる
      self->ws_->Close(on_close, self->config_.websocket_close_timeout);
      return;
    }
    on_close(
        boost::system::errc::make_error_code(boost::system::errc::success));
  });
}

void SoraSignaling::OnRedirect(boost::system::error_code ec,
                               std::string url,
                               std::shared_ptr<Websocket> ws) {
  if (state_ != State::Redirecting) {
    return;
  }

  if (ec) {
    SendOnDisconnect(SoraSignalingErrorCode::WEBSOCKET_HANDSHAKE_FAILED,
                     "Failed Websocket handshake in redirecting: ec=" +
                         ec.message() + " url=" + url);
    return;
  }

  boost::system::error_code tec;
  connection_timeout_timer_.cancel(tec);

  state_ = State::Connected;
  ws_ = ws;
  ws_connected_ = true;
  connected_signaling_url_.store(url);
  RTC_LOG(LS_INFO) << "Redirected: url=" << url;

  DoRead();
  DoSendConnect(true);
}

void SoraSignaling::DoRead() {
  ws_->Read([self = shared_from_this()](boost::system::error_code ec,
                                        std::size_t bytes_transferred,
                                        std::string text) {
    self->OnRead(ec, bytes_transferred, std::move(text));
  });
}

void SoraSignaling::DoSendConnect(bool redirect) {
  boost::json::object m = {
      {"type", "connect"},
      {"role", config_.role},
      {"channel_id", config_.channel_id},
      {"sora_client", Version::GetClientName()},
      {"libwebrtc", Version::GetLibwebrtcName()},
      {"environment", Version::GetEnvironmentName()},
  };

  if (redirect) {
    m["redirect"] = true;
  }

  if (!config_.client_id.empty()) {
    m["client_id"] = config_.client_id;
  }

  if (!config_.bundle_id.empty()) {
    m["bundle_id"] = config_.bundle_id;
  }

  if (!config_.sora_client.empty()) {
    m["sora_client"] = config_.sora_client;
  }

  if (config_.multistream) {
    m["multistream"] = *config_.multistream;
  }

  if (config_.simulcast) {
    m["simulcast"] = *config_.simulcast;
  }

  if (!config_.simulcast_rid.empty()) {
    m["simulcast_rid"] = config_.simulcast_rid;
  }

  if (config_.spotlight) {
    m["spotlight"] = *config_.spotlight;
  }
  if (config_.spotlight_number > 0) {
    m["spotlight_number"] = config_.spotlight_number;
  }

  if (!config_.spotlight_focus_rid.empty()) {
    m["spotlight_focus_rid"] = config_.spotlight_focus_rid;
  }

  if (!config_.spotlight_unfocus_rid.empty()) {
    m["spotlight_unfocus_rid"] = config_.spotlight_unfocus_rid;
  }

  if (!config_.metadata.is_null()) {
    m["metadata"] = config_.metadata;
  }

  if (!config_.signaling_notify_metadata.is_null()) {
    m["signaling_notify_metadata"] = config_.signaling_notify_metadata;
  }

  if (!config_.video) {
    // video: false の場合はそのまま設定
    m["video"] = false;
  } else {
    // video: true の場合は、ちゃんとオプションを設定する
    m["video"] = boost::json::object();
    if (!config_.video_codec_type.empty()) {
      m["video"].as_object()["codec_type"] = config_.video_codec_type;
    }
    if (config_.video_bit_rate != 0) {
      m["video"].as_object()["bit_rate"] = config_.video_bit_rate;
    }
    if (!config_.video_vp9_params.is_null()) {
      m["video"].as_object()["vp9_params"] = config_.video_vp9_params;
    }
    if (!config_.video_av1_params.is_null()) {
      m["video"].as_object()["av1_params"] = config_.video_av1_params;
    }
    if (!config_.video_h264_params.is_null()) {
      m["video"].as_object()["h264_params"] = config_.video_h264_params;
    }

    if (!config_.video_h265_params.is_null()) {
      m["video"].as_object()["h265_params"] = config_.video_h265_params;
    }

    // オプションの設定が行われてなければ単に true を設定
    if (m["video"].as_object().empty()) {
      m["video"] = true;
    }
  }

  if (!config_.audio) {
    m["audio"] = false;
  } else if (config_.audio && config_.audio_codec_type.empty() &&
             config_.audio_bit_rate == 0) {
    m["audio"] = true;
  } else {
    m["audio"] = boost::json::object();
    if (!config_.audio_codec_type.empty()) {
      m["audio"].as_object()["codec_type"] = config_.audio_codec_type;
    }
    if (config_.audio_bit_rate != 0) {
      m["audio"].as_object()["bit_rate"] = config_.audio_bit_rate;
    }
  }

  if (!config_.audio_streaming_language_code.empty()) {
    m["audio_streaming_language_code"] = config_.audio_streaming_language_code;
  }

  if (config_.data_channel_signaling) {
    m["data_channel_signaling"] = *config_.data_channel_signaling;
  }
  if (config_.ignore_disconnect_websocket) {
    m["ignore_disconnect_websocket"] = *config_.ignore_disconnect_websocket;
  }

  if (!config_.data_channels.empty()) {
    boost::json::array ar;
    for (const auto& d : config_.data_channels) {
      boost::json::object obj;
      obj["label"] = d.label;
      obj["direction"] = d.direction;
      if (d.ordered) {
        obj["ordered"] = *d.ordered;
      }
      if (d.max_packet_life_time) {
        obj["max_packet_life_time"] = *d.max_packet_life_time;
      }
      if (d.max_retransmits) {
        obj["max_retransmits"] = *d.max_retransmits;
      }
      if (d.protocol) {
        obj["protocol"] = *d.protocol;
      }
      if (d.compress) {
        obj["compress"] = *d.compress;
      }
      ar.push_back(obj);
    }
    m["data_channels"] = ar;
  }

  if (config_.forwarding_filter) {
    boost::json::object obj;
    auto& f = *config_.forwarding_filter;
    if (f.action) {
      obj["action"] = *f.action;
    }
    obj["rules"] = boost::json::array();
    for (const auto& rules : f.rules) {
      boost::json::array ar;
      for (const auto& r : rules) {
        boost::json::object rule;
        rule["field"] = r.field;
        rule["operator"] = r.op;
        rule["values"] = boost::json::array();
        for (const auto& v : r.values) {
          rule["values"].as_array().push_back(boost::json::value(v));
        }
        ar.push_back(rule);
      }
      obj["rules"].as_array().push_back(ar);
    }
    if (f.version) {
      obj["version"] = *f.version;
    }
    if (f.metadata) {
      obj["metadata"] = *f.metadata;
    }
    m["forwarding_filter"] = obj;
  }

  std::string text = boost::json::serialize(m);
  RTC_LOG(LS_INFO) << "Send type=connect: " << text;
  WsWriteSignaling(std::move(text), [self = shared_from_this()](
                                        boost::system::error_code, size_t) {});
}

void SoraSignaling::DoSendPong() {
  boost::json::value m = {{"type", "pong"}};
  ws_->WriteText(
      boost::json::serialize(m),
      [self = shared_from_this()](boost::system::error_code, size_t) {});
}

void SoraSignaling::DoSendPong(
    const rtc::scoped_refptr<const webrtc::RTCStatsReport>& report) {
  std::string stats = report->ToJson();
  if (dc_ && using_datachannel_ && dc_->IsOpen("stats")) {
    // DataChannel が使える場合は type: stats で DataChannel に送る
    std::string str = R"({"type":"stats","reports":)" + stats + "}";
    SendDataChannel("stats", str);
  } else if (ws_) {
    std::string str = R"({"type":"pong","stats":)" + stats + "}";
    ws_->WriteText(std::move(str), [self = shared_from_this()](
                                       boost::system::error_code, size_t) {});
  }
}

void SoraSignaling::DoSendUpdate(const std::string& sdp, std::string type) {
  boost::json::value m = {{"type", type}, {"sdp", sdp}};
  std::string text = boost::json::serialize(m);
  if (dc_ && using_datachannel_ && dc_->IsOpen("signaling")) {
    // DataChannel が使える場合は DataChannel に送る
    SendDataChannel("signaling", text);
    SendOnSignalingMessage(SoraSignalingType::DATACHANNEL,
                           SoraSignalingDirection::SENT, std::move(text));
  } else if (ws_) {
    WsWriteSignaling(
        std::move(text),
        [self = shared_from_this()](boost::system::error_code, size_t) {});
  }
}

class RawCryptString : public rtc::revive::CryptStringImpl {
 public:
  RawCryptString(const std::string& str) : str_(str) {}
  size_t GetLength() const override { return str_.size(); }
  void CopyTo(char* dest, bool nullterminate) const override {
    for (int i = 0; i < str_.size(); i++) {
      *dest++ = str_[i];
    }
    if (nullterminate) {
      *dest = '\0';
    }
  }
  std::string UrlEncode() const override { throw std::exception(); }
  CryptStringImpl* Copy() const override { return new RawCryptString(str_); }
  void CopyRawTo(std::vector<unsigned char>* dest) const override {
    dest->assign(str_.begin(), str_.end());
  }

 private:
  std::string str_;
};

rtc::scoped_refptr<webrtc::PeerConnectionInterface>
SoraSignaling::CreatePeerConnection(boost::json::value jconfig) {
  webrtc::PeerConnectionInterface::RTCConfiguration rtc_config;
  webrtc::PeerConnectionInterface::IceServers ice_servers;

  auto jservers = jconfig.at("iceServers");
  for (auto jserver : jservers.as_array()) {
    const std::string username = jserver.at("username").as_string().c_str();
    const std::string credential = jserver.at("credential").as_string().c_str();
    auto jurls = jserver.at("urls");
    for (const auto url : jurls.as_array()) {
      webrtc::PeerConnectionInterface::IceServer ice_server;
      ice_server.uri = url.as_string().c_str();
      ice_server.username = username;
      ice_server.password = credential;
      ice_servers.push_back(ice_server);
    }
  }

  rtc_config.servers = ice_servers;

  // macOS のサイマルキャスト時、なぜか無限に解像度が落ちていくので、
  // それを回避するために cpu_adaptation を無効にする。
#if defined(__APPLE__)
  if (offer_config_.simulcast) {
    rtc_config.set_cpu_adaptation(false);
  }
#endif

  rtc_config.sdp_semantics = webrtc::SdpSemantics::kUnifiedPlan;
  webrtc::PeerConnectionDependencies dependencies(this);

  // WebRTC の SSL 接続の検証は自前のルート証明書(rtc_base/ssl_roots.h)でやっていて、
  // その中に Let's Encrypt の証明書が無いため、接続先によっては接続できないことがある。
  //
  // それを解消するために tls_cert_verifier を設定して自前で検証を行う。
  dependencies.tls_cert_verifier = std::unique_ptr<rtc::SSLCertificateVerifier>(
      new RTCSSLVerifier(config_.insecure, config_.ca_cert));

  // Proxy を設定する
  if (!config_.proxy_url.empty() && config_.network_manager != nullptr &&
      config_.socket_factory) {
    dependencies.allocator.reset(new cricket::BasicPortAllocator(
        config_.network_manager, config_.socket_factory,
        rtc_config.turn_customizer));
    dependencies.allocator->SetPortRange(
        rtc_config.port_allocator_config.min_port,
        rtc_config.port_allocator_config.max_port);
    dependencies.allocator->set_flags(rtc_config.port_allocator_config.flags);

    RTC_LOG(LS_INFO) << "Set Proxy: type="
                     << rtc::revive::ProxyToString(rtc::revive::PROXY_HTTPS)
                     << " url=" << config_.proxy_url
                     << " username=" << config_.proxy_username;
    rtc::revive::ProxyInfo pi;
    pi.type = rtc::revive::PROXY_HTTPS;
    URLParts parts;
    if (!URLParts::Parse(config_.proxy_url, parts)) {
      RTC_LOG(LS_ERROR) << "Failed to parse: proxy_url=" << config_.proxy_url;
      return nullptr;
    }
    pi.address = rtc::SocketAddress(parts.host, std::stoi(parts.GetPort()));
    if (!config_.proxy_username.empty()) {
      pi.username = config_.proxy_username;
    }
    if (!config_.proxy_password.empty()) {
      pi.password =
          rtc::revive::CryptString(RawCryptString(config_.proxy_password));
    }
    std::string proxy_agent = "Sora C++ SDK";
    if (!config_.proxy_agent.empty()) {
      proxy_agent = config_.proxy_agent;
    }
    dependencies.allocator->set_proxy(proxy_agent, pi);
  }

  webrtc::RTCErrorOr<rtc::scoped_refptr<webrtc::PeerConnectionInterface>>
      connection = config_.pc_factory->CreatePeerConnectionOrError(
          rtc_config, std::move(dependencies));
  if (!connection.ok()) {
    RTC_LOG(LS_ERROR) << "CreatePeerConnection failed: errro="
                      << connection.error().message();
    return nullptr;
  }

  return connection.value();
}

void SoraSignaling::DoInternalDisconnect(
    boost::optional<SoraSignalingErrorCode> force_error_code,
    std::string reason,
    std::string message) {
  assert(state_ == State::Connected);

  state_ = State::Closing;

  auto on_close = [self = shared_from_this(), force_error_code,
                   root_message = message](bool succeeded,
                                           SoraSignalingErrorCode error_code,
                                           std::string message) {
    if (force_error_code == boost::none) {
      self->SendOnDisconnect(error_code, message);
    } else {
      self->SendOnDisconnect(*force_error_code,
                             root_message + ", internal_message=" + message);
    }
  };

  // Close 処理中に、意図しない場所で WS Close が呼ばれた場合の対策。
  // 例えば dc_->Close()→送信完了して on_ws_close_ に値を設定して切断を待つ、
  // となるまでの間に WS Close された場合、on_ws_close_ に値が設定されていなくて
  // 永遠に終了できなくなってしまう。
  if (ws_connected_) {
    on_ws_close_ = [self = shared_from_this(),
                    on_close](boost::system::error_code ec) {
      boost::system::error_code tec;
      self->closing_timeout_timer_.cancel(tec);
      auto ws_reason = self->ws_->reason();
      self->SendOnWsClose(ws_reason);
      std::string message =
          "Unintended disconnect WebSocket during Disconnect process: ec=" +
          ec.message() + " wscode=" + std::to_string(ws_reason.code) +
          " wsreason=" + ws_reason.reason.c_str();
      on_close(false, SoraSignalingErrorCode::CLOSE_FAILED, message);
    };
  }

  if (using_datachannel_ && ws_connected_) {
    std::string text =
        force_error_code == boost::none
            ? R"({"type":"disconnect","reason":"NO-ERROR"})"
            : R"({"type":"disconnect","reason":")" + reason + "\"}";
    webrtc::DataBuffer disconnect = ConvertToDataBuffer("signaling", text);
    dc_->Close(
        disconnect,
        [self = shared_from_this(), on_close, force_error_code,
         message](boost::system::error_code ec1) {
          self->closing_timeout_timer_.expires_from_now(
              boost::posix_time::seconds(
                  self->config_.websocket_close_timeout));
          self->closing_timeout_timer_.async_wait(
              [self](boost::system::error_code ec) {
                if (ec) {
                  return;
                }
                self->ws_->Cancel();
              });
          self->on_ws_close_ = [self, ec1,
                                on_close](boost::system::error_code ec2) {
            boost::system::error_code tec;
            self->closing_timeout_timer_.cancel(tec);
            auto ws_reason = self->ws_->reason();
            self->SendOnWsClose(ws_reason);
            std::string ws_reason_str =
                " wscode=" + std::to_string(ws_reason.code) +
                " wsreason=" + ws_reason.reason.c_str();

            bool ec2_error = ec2 != boost::beast::websocket::error::closed;
            bool succeeded = true;
            std::string message =
                "Succeeded to close DataChannel and Websocket";
            auto error_code = SoraSignalingErrorCode::CLOSE_SUCCEEDED;
            if (ec1 && ec2_error) {
              succeeded = false;
              message = "Failed to close DataChannel and WebSocket: ec1=" +
                        ec1.message() + " ec2=" + ec2.message() + ws_reason_str;
              error_code = SoraSignalingErrorCode::CLOSE_FAILED;
            } else if (ec1 && !ec2_error) {
              succeeded = false;
              message = "Failed to close DataChannel (WS succeeded): ec=" +
                        ec1.message() + ws_reason_str;
              error_code = SoraSignalingErrorCode::CLOSE_FAILED;
            } else if (!ec1 && ec2_error) {
              succeeded = false;
              message = "Failed to close WebSocket (DC succeeded): ec=" +
                        ec2.message() + ws_reason_str;
              error_code = SoraSignalingErrorCode::CLOSE_FAILED;
            }
            on_close(succeeded, error_code, message);
          };
        },
        config_.disconnect_wait_timeout);

    SendOnSignalingMessage(SoraSignalingType::DATACHANNEL,
                           SoraSignalingDirection::SENT, std::move(text));
  } else if (using_datachannel_ && !ws_connected_) {
    std::string text = R"({"type":"disconnect","reason":"NO-ERROR"})";
    webrtc::DataBuffer disconnect = ConvertToDataBuffer("signaling", text);
    dc_->Close(
        disconnect,
        [on_close](boost::system::error_code ec) {
          if (ec) {
            on_close(false, SoraSignalingErrorCode::CLOSE_FAILED,
                     "Failed to close DataChannel: ec=" + ec.message());
            return;
          }
          on_close(true, SoraSignalingErrorCode::CLOSE_SUCCEEDED,
                   "Succeeded to close DataChannel");
        },
        config_.disconnect_wait_timeout);

    SendOnSignalingMessage(SoraSignalingType::DATACHANNEL,
                           SoraSignalingDirection::SENT, std::move(text));
  } else if (!using_datachannel_ && ws_connected_) {
    boost::json::value disconnect = {{"type", "disconnect"},
                                     {"reason", "NO-ERROR"}};
    WsWriteSignaling(
        boost::json::serialize(disconnect),
        [self = shared_from_this(), on_close](boost::system::error_code ec,
                                              std::size_t) {
          if (ec) {
            on_close(
                false, SoraSignalingErrorCode::CLOSE_FAILED,
                "Failed to write disconnect message to close WebSocket: ec=" +
                    ec.message());
            return;
          }

          self->closing_timeout_timer_.expires_from_now(
              boost::posix_time::seconds(
                  self->config_.websocket_close_timeout));
          self->closing_timeout_timer_.async_wait(
              [self](boost::system::error_code ec) {
                if (ec) {
                  return;
                }
                self->ws_->Cancel();
              });
          self->on_ws_close_ = [self, on_close](boost::system::error_code ec) {
            boost::system::error_code tec;
            self->closing_timeout_timer_.cancel(tec);
            auto reason = self->ws_->reason();
            self->SendOnWsClose(reason);
            bool ec_error = ec != boost::beast::websocket::error::closed;
            if (ec_error) {
              on_close(false, SoraSignalingErrorCode::CLOSE_FAILED,
                       "Failed to close WebSocket: ec=" + ec.message() +
                           " wscode=" + std::to_string(reason.code) +
                           " wsreason=" + reason.reason.c_str());
              return;
            }
            on_close(true, SoraSignalingErrorCode::CLOSE_SUCCEEDED,
                     "Succeeded to close WebSocket");
          };
        });
  } else {
    on_close(false, SoraSignalingErrorCode::INTERNAL_ERROR,
             "Unknown state. WS and DC is already released.");
  }
}

std::function<void(webrtc::RTCError)> SoraSignaling::CreateIceError(
    std::string message) {
  return [self = shared_from_this(), message](webrtc::RTCError error) {
    boost::asio::post(*self->config_.io_context, [self, message, error]() {
      if (self->state_ != State::Connected) {
        return;
      }
      self->DoInternalDisconnect(SoraSignalingErrorCode::ICE_FAILED,
                                 "INTERNAL-ERROR",
                                 message + ": error=" + error.message());
    });
  };
}

void SoraSignaling::OnConnect(boost::system::error_code ec,
                              std::string url,
                              std::shared_ptr<Websocket> ws) {
  RTC_LOG(LS_INFO) << "OnConnect url=" << url;

  connecting_wss_.erase(
      std::remove_if(connecting_wss_.begin(), connecting_wss_.end(),
                     [ws](std::shared_ptr<Websocket> p) { return p == ws; }),
      connecting_wss_.end());

  if (state_ == State::Closed) {
    return;
  }

  if (ec) {
    RTC_LOG(LS_WARNING) << "Failed Websocket handshake: " << ec
                        << " url=" << url << " state=" << (int)state_
                        << " wss_len=" << connecting_wss_.size();
    // すべての接続がうまくいかなかったら終了する
    if (state_ == State::Connecting && connecting_wss_.empty()) {
      SendOnDisconnect(
          SoraSignalingErrorCode::WEBSOCKET_HANDSHAKE_FAILED,
          "Failed Websocket handshake: last_ec=" + ec.message() +
              " last_url=" + url +
              (config_.proxy_url.empty() ? ""
                                         : " proxy_url=" + config_.proxy_url));
    }
    return;
  }

  if (state_ == State::Connected || state_ == State::Redirecting) {
    // 既に他の接続が先に完了していたので、切断する
    ws->Close([self = shared_from_this(), ws](boost::system::error_code) {},
              config_.websocket_close_timeout);
    return;
  }

  boost::system::error_code tec;
  connection_timeout_timer_.cancel(tec);

  RTC_LOG(LS_INFO) << "Signaling Websocket is connected: url=" << url;
  state_ = State::Connected;
  ws_ = ws;
  ws_connected_ = true;
  selected_signaling_url_.store(url);
  connected_signaling_url_.store(url);
  RTC_LOG(LS_INFO) << "Connected: url=" << url;

  DoRead();
  DoSendConnect(false);
}

void SoraSignaling::OnRead(boost::system::error_code ec,
                           std::size_t bytes_transferred,
                           std::string text) {
  boost::ignore_unused(bytes_transferred);

  if (ec) {
    assert(state_ == State::Connected || state_ == State::Closing ||
           state_ == State::Closed);
    if (state_ == State::Closed) {
      // 全て終わってるので何もしない
      return;
    }
    if (state_ == State::Closing) {
      if (on_ws_close_) {
        // Close で切断されて呼ばれたコールバックなので、on_ws_close_ を呼んで続きを処理してもらう
        on_ws_close_(ec);
        return;
      }
      // ここに来ることは無いはず
      RTC_LOG(LS_ERROR) << "OnRead: state is Closing but on_ws_close_ is null";
      return;
    }
    if (state_ == State::Connected && !using_datachannel_) {
      // 何かエラーが起きたので切断する
      ws_connected_ = false;
      auto error_code = ec == boost::beast::websocket::error::closed
                            ? SoraSignalingErrorCode::WEBSOCKET_ONCLOSE
                            : SoraSignalingErrorCode::WEBSOCKET_ONERROR;
      auto reason = ws_->reason();
      SendOnWsClose(reason);
      std::string reason_str = " wscode=" + std::to_string(reason.code) +
                               " wsreason=" + reason.reason.c_str();
      SendOnDisconnect(error_code, "Failed to read WebSocket: ec=" +
                                       ec.message() + reason_str);
      return;
    }
    if (state_ == State::Connected && using_datachannel_ && !ws_connected_) {
      // ignore_disconnect_websocket による WS 切断なので何もしない
      return;
    }
    if (state_ == State::Connected && using_datachannel_ && ws_connected_) {
      // DataChanel で reason: "WEBSOCKET-ONCLOSE" または "WEBSOCKET-ONERROR" を送る事を試みてから終了する
      std::string text =
          ec == boost::beast::websocket::error::closed
              ? R"({"type":"disconnect","reason":"WEBSOCKET-ONCLOSE"})"
              : R"({"type":"disconnect","reason":"WEBSOCKET-ONERROR"})";
      webrtc::DataBuffer disconnect = ConvertToDataBuffer("signaling", text);
      auto error_code = ec == boost::beast::websocket::error::closed
                            ? SoraSignalingErrorCode::WEBSOCKET_ONCLOSE
                            : SoraSignalingErrorCode::WEBSOCKET_ONERROR;
      auto reason = ws_->reason();
      std::string reason_str = " wscode=" + std::to_string(reason.code) +
                               " wsreason=" + reason.reason.c_str();
      state_ = State::Closing;
      dc_->Close(
          disconnect,
          [self = shared_from_this(), ec, error_code,
           reason_str](boost::system::error_code ec2) {
            if (ec2) {
              self->SendOnDisconnect(
                  error_code,
                  "Failed to read WebSocket and to close DataChannel: ec=" +
                      ec.message() + " ec2=" + ec2.message() + reason_str);
              return;
            }
            self->SendOnDisconnect(
                error_code,
                "Failed to read WebSocket and succeeded to close "
                "DataChannel: ec=" +
                    ec.message() + reason_str);
          },
          config_.disconnect_wait_timeout);

      SendOnSignalingMessage(SoraSignalingType::DATACHANNEL,
                             SoraSignalingDirection::SENT, std::move(text));
      return;
    }

    // ここに来ることは無いはず
    SendOnDisconnect(SoraSignalingErrorCode::INTERNAL_ERROR,
                     "Failed to read WebSocket: ec=" + ec.message());
    return;
  }

  if (state_ != State::Connected) {
    DoRead();
    return;
  }

  RTC_LOG(LS_INFO) << "OnRead: text=" << text;

  auto m = boost::json::parse(text);
  const std::string type = m.at("type").as_string().c_str();

  // pc_ が初期化される前に offer, redirect 以外がやってきたら単に無視する
  if (type != "offer" && type != "redirect" && pc_ == nullptr) {
    DoRead();
    return;
  }

  if (type == "redirect") {
    SendOnSignalingMessage(SoraSignalingType::WEBSOCKET,
                           SoraSignalingDirection::RECEIVED, std::move(text));

    const std::string location = m.at("location").as_string().c_str();
    Redirect(location);
    // Redirect の中で次の Read をしているのでここで return する
    return;
  } else if (type == "offer") {
    // 後続で使っているので text はコピーする
    SendOnSignalingMessage(SoraSignalingType::WEBSOCKET,
                           SoraSignalingDirection::RECEIVED, text);

    const std::string sdp = m.at("sdp").as_string().c_str();

    std::string video_mid;
    std::string audio_mid;
    {
      auto it = m.as_object().find("mid");
      if (it != m.as_object().end()) {
        const auto& midobj = it->value().as_object();
        auto mit = midobj.find("video");
        if (mit != midobj.end()) {
          video_mid = mit->value().as_string().c_str();
        }
        mit = midobj.find("audio");
        if (mit != midobj.end()) {
          audio_mid = mit->value().as_string().c_str();
        }
      }
    }
    RTC_LOG(LS_INFO) << "video mid: " << video_mid;
    RTC_LOG(LS_INFO) << "audio mid: " << audio_mid;
    video_mid_ = video_mid;
    audio_mid_ = audio_mid;

    if (!CheckSdp(sdp)) {
      return;
    }

    {
      boost::json::object::iterator it;
      it = m.as_object().find("multistream");
      if (it != m.as_object().end()) {
        offer_config_.multistream = it->value().as_bool();
      }
      it = m.as_object().find("simulcast");
      if (it != m.as_object().end()) {
        offer_config_.simulcast = it->value().as_bool();
      }
      it = m.as_object().find("spotlight");
      if (it != m.as_object().end()) {
        offer_config_.spotlight = it->value().as_bool();
      }
    }

    // Data Channel の圧縮されたデータが送られてくるラベルを覚えておく
    {
      auto it = m.as_object().find("data_channels");
      if (it != m.as_object().end()) {
        const auto& ar = it->value().as_array();
        for (const auto& v : ar) {
          std::string label = v.at("label").as_string().c_str();
          DataChannelInfo info;
          info.compressed = v.at("compress").as_bool();
          dc_labels_.insert(std::make_pair(label, info));
        }
      }
    }

    connection_id_ = m.at("connection_id").as_string().c_str();

    pc_ = CreatePeerConnection(m.at("config"));

    SessionDescription::SetOffer(
        pc_.get(), sdp,
        [self = shared_from_this(), m, text]() {
          boost::asio::post(*self->config_.io_context, [self, m, text]() {
            if (self->state_ != State::Connected) {
              return;
            }

            // simulcast では offer の setRemoteDescription が終わった後に
            // トラックを追加する必要があるため、ここで初期化する
            auto ob = self->config_.observer.lock();
            if (ob != nullptr) {
              ob->OnSetOffer(std::move(text));
            }

            if (self->offer_config_.simulcast &&
                m.as_object().count("encodings") != 0) {
              std::vector<webrtc::RtpEncodingParameters> encoding_parameters;

              // "encodings" キーの各内容を webrtc::RtpEncodingParameters に変換する
              auto encodings_json = m.at("encodings").as_array();
              for (auto v : encodings_json) {
                auto p = v.as_object();
                webrtc::RtpEncodingParameters params;
                // absl::optional<uint32_t> ssrc;
                // double bitrate_priority = kDefaultBitratePriority;
                // enum class Priority { kVeryLow, kLow, kMedium, kHigh };
                // Priority network_priority = Priority::kLow;
                // absl::optional<int> max_bitrate_bps;
                // absl::optional<int> min_bitrate_bps;
                // absl::optional<double> max_framerate;
                // absl::optional<int> num_temporal_layers;
                // absl::optional<double> scale_resolution_down_by;
                // bool active = true;
                // std::string rid;
                // bool adaptive_ptime = false;
                params.rid = p["rid"].as_string().c_str();
                if (p.count("maxBitrate") != 0) {
                  params.max_bitrate_bps = p["maxBitrate"].to_number<int>();
                }
                if (p.count("minBitrate") != 0) {
                  params.min_bitrate_bps = p["minBitrate"].to_number<int>();
                }
                if (p.count("scaleResolutionDownBy") != 0) {
                  params.scale_resolution_down_by =
                      p["scaleResolutionDownBy"].to_number<double>();
                }
                if (p.count("maxFramerate") != 0) {
                  params.max_framerate = p["maxFramerate"].to_number<double>();
                }
                if (p.count("active") != 0) {
                  params.active = p["active"].as_bool();
                }
                if (p.count("adaptivePtime") != 0) {
                  params.adaptive_ptime = p["adaptivePtime"].as_bool();
                }
                if (p.count("scalabilityMode") != 0) {
                  params.scalability_mode =
                      p["scalabilityMode"].as_string().c_str();
                }
                encoding_parameters.push_back(params);
              }

              self->SetEncodingParameters(self->video_mid_,
                                          std::move(encoding_parameters));
            }

            SessionDescription::CreateAnswer(
                self->pc_.get(),
                [self](webrtc::SessionDescriptionInterface* desc) {
                  std::string sdp;
                  desc->ToString(&sdp);
                  //self->manager_->SetParameters();
                  boost::asio::post(*self->config_.io_context, [self, sdp]() {
                    if (!self->pc_) {
                      return;
                    }

                    boost::json::value m = {{"type", "answer"}, {"sdp", sdp}};
                    self->WsWriteSignaling(
                        boost::json::serialize(m),
                        [self](boost::system::error_code, size_t) {});
                  });
                },
                self->CreateIceError("Failed to CreateAnswer in offer "
                                     "message via WebSocket"));
          });
        },
        CreateIceError("Failed to SetOffer in offer message via WebSocket"));
  } else if (type == "update" || type == "re-offer") {
    if (pc_ == nullptr) {
      return;
    }

    SendOnSignalingMessage(SoraSignalingType::WEBSOCKET,
                           SoraSignalingDirection::RECEIVED, std::move(text));

    std::string answer_type = type == "update" ? "update" : "re-answer";
    const std::string sdp = m.at("sdp").as_string().c_str();
    if (!CheckSdp(sdp)) {
      return;
    }

    SessionDescription::SetOffer(
        pc_.get(), sdp,
        [self = shared_from_this(), type, answer_type]() {
          boost::asio::post(*self->config_.io_context, [self, type,
                                                        answer_type]() {
            if (!self->pc_) {
              return;
            }

            // エンコーディングパラメータの情報がクリアされるので設定し直す
            if (self->offer_config_.simulcast) {
              self->ResetEncodingParameters();
            }

            SessionDescription::CreateAnswer(
                self->pc_.get(),
                [self, answer_type](webrtc::SessionDescriptionInterface* desc) {
                  std::string sdp;
                  desc->ToString(&sdp);
                  //self->manager_->SetParameters();
                  boost::asio::post(*self->config_.io_context,
                                    [self, sdp, answer_type]() {
                                      if (!self->pc_) {
                                        return;
                                      }

                                      self->DoSendUpdate(sdp, answer_type);
                                    });
                },
                self->CreateIceError("Failed to CreateAnswer in " + type +
                                     " message via WebSocket"));
          });
        },
        CreateIceError("Failed to SetOffer in " + type +
                       " message via WebSocket"));
  } else if (type == "notify") {
    auto ob = config_.observer.lock();
    if (ob) {
      ob->OnNotify(std::move(text));
    }
  } else if (type == "push") {
    auto ob = config_.observer.lock();
    if (ob) {
      ob->OnPush(std::move(text));
    }
  } else if (type == "ping") {
    auto it = m.as_object().find("stats");
    if (it != m.as_object().end() && it->value().as_bool()) {
      pc_->GetStats(
          RTCStatsCallback::Create(
              [self = shared_from_this()](
                  const rtc::scoped_refptr<const webrtc::RTCStatsReport>&
                      report) {
                if (self->state_ != State::Connected) {
                  return;
                }

                self->DoSendPong(report);
              })
              .get());
    } else {
      DoSendPong();
    }
  } else if (type == "switched") {
    // Data Channel による通信の開始
    using_datachannel_ = true;

    auto ob = config_.observer.lock();
    if (ob) {
      ob->OnSwitched(std::move(text));
    }

    // ignore_disconnect_websocket == true の場合は WS を切断する
    auto it = m.as_object().find("ignore_disconnect_websocket");
    if (it != m.as_object().end() && it->value().as_bool() && ws_connected_) {
      RTC_LOG(LS_INFO) << "Close WebSocket for DataChannel";
      ws_->Close(
          [self = shared_from_this()](boost::system::error_code ec) {
            if (!ec) {
              self->SendOnWsClose(boost::beast::websocket::close_reason(
                  boost::beast::websocket::close_code::normal));
            }
          },
          config_.websocket_close_timeout);
      ws_connected_ = false;

      return;
    }
  }
  DoRead();
}

void SoraSignaling::DoConnect() {
  if (state_ != State::Init && state_ != State::Closed) {
    return;
  }

  dc_.reset(new DataChannel(*config_.io_context, shared_from_this()));

  // 接続タイムアウト用の処理
  connection_timeout_timer_.expires_from_now(boost::posix_time::seconds(30));
  connection_timeout_timer_.async_wait(
      [self = shared_from_this()](boost::system::error_code ec) {
        if (ec) {
          return;
        }

        self->SendOnDisconnect(SoraSignalingErrorCode::INTERNAL_ERROR,
                               "Connection timeout");
      });

  auto signaling_urls = config_.signaling_urls;
  if (!config_.disable_signaling_url_randomization) {
    // ランダムに並び替える
    std::random_device seed_gen;
    std::mt19937 engine(seed_gen());
    std::shuffle(signaling_urls.begin(), signaling_urls.end(), engine);
  }

  state_ = State::Connecting;

  std::string error_messages;
  for (const auto& url : signaling_urls) {
    URLParts parts;
    bool ssl;
    if (!ParseURL(url, parts, ssl)) {
      RTC_LOG(LS_WARNING) << "Invalid Signaling URL: " << url;
      error_messages += "Invalid Signaling URL: " + url + " | ";
      continue;
    }

    std::shared_ptr<Websocket> ws;
    if (ssl) {
      if (config_.proxy_url.empty()) {
        ws.reset(new Websocket(Websocket::ssl_tag(), *config_.io_context,
                               config_.insecure, config_.client_cert,
                               config_.client_key, config_.ca_cert));
      } else {
        ws.reset(new Websocket(
            Websocket::https_proxy_tag(), *config_.io_context, config_.insecure,
            config_.client_cert, config_.client_key, config_.ca_cert,
            config_.proxy_url, config_.proxy_username, config_.proxy_password));
      }
    } else {
      ws.reset(new Websocket(*config_.io_context));
    }
    if (config_.user_agent != boost::none) {
      ws->SetUserAgent(*config_.user_agent);
    }
    ws->Connect(url, std::bind(&SoraSignaling::OnConnect, shared_from_this(),
                               std::placeholders::_1, url, ws));
    connecting_wss_.push_back(ws);
  }
  if (connecting_wss_.empty()) {
    SendOnDisconnect(SoraSignalingErrorCode::INVALID_PARAMETER, error_messages);
    return;
  }
}

void SoraSignaling::SetEncodingParameters(
    std::string mid,
    std::vector<webrtc::RtpEncodingParameters> encodings) {
  for (auto transceiver : pc_->GetTransceivers()) {
    RTC_LOG(LS_INFO) << "transceiver mid="
                     << transceiver->mid().value_or("nullopt") << " direction="
                     << webrtc::RtpTransceiverDirectionToString(
                            transceiver->direction())
                     << " current_direction="
                     << (transceiver->current_direction()
                             ? webrtc::RtpTransceiverDirectionToString(
                                   *transceiver->current_direction())
                             : "nullopt")
                     << " media_type="
                     << cricket::MediaTypeToString(transceiver->media_type())
                     << " sender_encoding_count="
                     << transceiver->sender()->GetParameters().encodings.size();
  }

  for (auto enc : encodings) {
    RTC_LOG(LS_INFO) << "SetEncodingParameters: rid=" << enc.rid
                     << " active=" << (enc.active ? "true" : "false")
                     << " max_framerate="
                     << (enc.max_framerate ? std::to_string(*enc.max_framerate)
                                           : std::string("nullopt"))
                     << " scale_resolution_down_by="
                     << (enc.scale_resolution_down_by
                             ? std::to_string(*enc.scale_resolution_down_by)
                             : std::string("nullopt"));
  }

  rtc::scoped_refptr<webrtc::RtpTransceiverInterface> video_transceiver;
  for (auto transceiver : pc_->GetTransceivers()) {
    if (transceiver->mid() && *transceiver->mid() == mid) {
      video_transceiver = transceiver;
      break;
    }
  }

  if (video_transceiver == nullptr) {
    RTC_LOG(LS_ERROR) << "video transceiver not found";
    return;
  }

  rtc::scoped_refptr<webrtc::RtpSenderInterface> sender =
      video_transceiver->sender();
  webrtc::RtpParameters parameters = sender->GetParameters();
  parameters.encodings = encodings;
  sender->SetParameters(parameters);

  encodings_ = encodings;
}

void SoraSignaling::ResetEncodingParameters() {
  if (encodings_.empty() || video_mid_.empty()) {
    return;
  }

  for (auto enc : encodings_) {
    RTC_LOG(LS_INFO) << "ResetEncodingParameters: rid=" << enc.rid
                     << " active=" << (enc.active ? "true" : "false")
                     << " max_framerate="
                     << (enc.max_framerate ? std::to_string(*enc.max_framerate)
                                           : std::string("nullopt"))
                     << " scale_resolution_down_by="
                     << (enc.scale_resolution_down_by
                             ? std::to_string(*enc.scale_resolution_down_by)
                             : std::string("nullopt"));
  }

  rtc::scoped_refptr<webrtc::RtpTransceiverInterface> video_transceiver;
  for (auto transceiver : pc_->GetTransceivers()) {
    if (transceiver->mid() == video_mid_) {
      video_transceiver = transceiver;
      break;
    }
  }

  if (video_transceiver == nullptr) {
    RTC_LOG(LS_ERROR) << "video transceiver not found";
    return;
  }

  rtc::scoped_refptr<webrtc::RtpSenderInterface> sender =
      video_transceiver->sender();
  webrtc::RtpParameters parameters = sender->GetParameters();
  std::vector<webrtc::RtpEncodingParameters> new_encodings = encodings_;

  // ssrc を上書きする
  for (auto& enc : new_encodings) {
    auto it =
        std::find_if(parameters.encodings.begin(), parameters.encodings.end(),
                     [&enc](const webrtc::RtpEncodingParameters& p) {
                       return p.rid == enc.rid;
                     });
    if (it == parameters.encodings.end()) {
      RTC_LOG(LS_WARNING) << "Specified rid [" << enc.rid << "] not found";
      return;
    }
    RTC_LOG(LS_INFO) << "Set ssrc: rid=" << enc.rid << " ssrc="
                     << (it->ssrc ? std::to_string(*it->ssrc)
                                  : std::string("nullopt"));
    enc.ssrc = it->ssrc;
  }
  parameters.encodings = new_encodings;
  sender->SetParameters(parameters);
}

void SoraSignaling::WsWriteSignaling(std::string text,
                                     Websocket::write_callback_t on_write) {
  ws_->WriteText(text, on_write);

  SendOnSignalingMessage(SoraSignalingType::WEBSOCKET,
                         SoraSignalingDirection::SENT, std::move(text));
}

void SoraSignaling::SendOnDisconnect(SoraSignalingErrorCode ec,
                                     std::string message) {
  if (ec != SoraSignalingErrorCode::CLOSE_SUCCEEDED) {
    RTC_LOG(LS_ERROR) << "Failed to Disconnect: message=" << message;
  }
  boost::asio::post(*config_.io_context, [self = shared_from_this(), ec,
                                          message = std::move(message)]() {
    self->Clear();
    auto ob = self->config_.observer.lock();
    if (ob != nullptr) {
      ob->OnDisconnect(ec, std::move(message));
    }
  });
}

void SoraSignaling::SendOnSignalingMessage(SoraSignalingType type,
                                           SoraSignalingDirection direction,
                                           std::string message) {
  if (auto ob = config_.observer.lock(); ob) {
    ob->OnSignalingMessage(type, direction, std::move(message));
  }
}

void SoraSignaling::SendOnWsClose(
    const boost::beast::websocket::close_reason& reason) {
  if (auto ob = config_.observer.lock(); ob) {
    ob->OnWsClose(reason.code, reason.reason.c_str());
  }
}

webrtc::DataBuffer SoraSignaling::ConvertToDataBuffer(
    const std::string& label,
    const std::string& input) {
  auto it = dc_labels_.find(label);
  bool compressed = it != dc_labels_.end() && it->second.compressed;
  RTC_LOG(LS_INFO) << "Convert to DataChannel label=" << label
                   << " compressed=" << compressed << " input=" << input;
  const std::string& str = compressed ? ZlibHelper::Compress(input) : input;
  return webrtc::DataBuffer(rtc::CopyOnWriteBuffer(str), true);
}

bool SoraSignaling::SendDataChannel(const std::string& label,
                                    const std::string& input) {
  if (dc_ == nullptr) {
    return false;
  }

  webrtc::DataBuffer data = ConvertToDataBuffer(label, input);
  dc_->Send(label, data);
  return true;
}

void SoraSignaling::Clear() {
  boost::system::error_code tec;
  connection_timeout_timer_.cancel(tec);
  closing_timeout_timer_.cancel(tec);
  connecting_wss_.clear();
  selected_signaling_url_.store("");
  connected_signaling_url_.store("");
  pc_ = nullptr;
  ws_connected_ = false;
  ws_ = nullptr;
  using_datachannel_ = false;
  dc_ = nullptr;
  dc_labels_.clear();
  encodings_.clear();
  video_mid_.clear();
  on_ws_close_ = nullptr;
  ice_state_ = webrtc::PeerConnectionInterface::kIceConnectionNew;
  connection_state_ =
      webrtc::PeerConnectionInterface::PeerConnectionState::kNew;
  state_ = State::Closed;
}

// --------------------------------
// webrtc::PeerConnectionObserver の実装
// --------------------------------

void SoraSignaling::OnDataChannel(
    rtc::scoped_refptr<webrtc::DataChannelInterface> data_channel) {
  if (dc_ == nullptr) {
    return;
  }
  dc_->AddDataChannel(data_channel);
}

void SoraSignaling::OnStandardizedIceConnectionChange(
    webrtc::PeerConnectionInterface::IceConnectionState new_state) {
  boost::asio::post(
      *config_.io_context, [self = shared_from_this(), new_state]() {
        RTC_LOG(LS_INFO) << "IceConnectionState changed: ["
                         << webrtc::PeerConnectionInterface::AsString(
                                self->ice_state_)
                         << "]->["
                         << webrtc::PeerConnectionInterface::AsString(new_state)
                         << "]";
        self->ice_state_ = new_state;
      });
}

void SoraSignaling::OnConnectionChange(
    webrtc::PeerConnectionInterface::PeerConnectionState new_state) {
  boost::asio::post(
      *config_.io_context, [self = shared_from_this(), new_state]() {
        RTC_LOG(LS_INFO) << "ConnectionChange: ["
                         << webrtc::PeerConnectionInterface::AsString(
                                self->connection_state_)
                         << "]->["
                         << webrtc::PeerConnectionInterface::AsString(new_state)
                         << "]";
        self->connection_state_ = new_state;

        // Failed になったら諦めるしか無いので終了処理に入る
        if (new_state ==
            webrtc::PeerConnectionInterface::PeerConnectionState::kFailed) {
          if (self->state_ != State::Connected) {
            // この場合別のフローから終了処理に入ってるはずなので無視する
            return;
          }
          // disconnect を送る必要は無い（そもそも failed になってるということは届かない）のでそのまま落とすだけ
          self->SendOnDisconnect(
              SoraSignalingErrorCode::PEER_CONNECTION_STATE_FAILED,
              "PeerConnectionState::kFailed");
        }
      });
}

void SoraSignaling::OnIceCandidate(
    const webrtc::IceCandidateInterface* candidate) {
  std::string sdp;
  if (!candidate->ToString(&sdp)) {
    boost::asio::post(*config_.io_context, [self = shared_from_this()]() {
      if (self->state_ != State::Connected) {
        return;
      }
      self->DoInternalDisconnect(SoraSignalingErrorCode::INTERNAL_ERROR,
                                 "INTERNAL-ERROR",
                                 "Failed to serialize candidate");
    });
    return;
  }

  boost::json::value m = {{"type", "candidate"}, {"candidate", sdp}};
  boost::asio::post(
      *config_.io_context, [self = shared_from_this(), m = std::move(m)]() {
        if (self->state_ != State::Connected) {
          return;
        }

        self->WsWriteSignaling(boost::json::serialize(m),
                               [self](boost::system::error_code, size_t) {});
      });
}

void SoraSignaling::OnIceCandidateError(const std::string& address,
                                        int port,
                                        const std::string& url,
                                        int error_code,
                                        const std::string& error_text) {
  // boost::asio::post([self = shared_from_this(), address, port, url,
  //                    error_code, error_text]() {
  //   if (self->state_ != State::Connected) {
  //     return;
  //   }
  //   self->DoInternalDisconnect(
  //       SoraSignalingErrorCode::ICE_FAILED, "INTERNAL-ERROR",
  //       "address=" + address + " port=" + std::to_string(port) +
  //           " url=" + url + " error_code=" + std::to_string(error_code) +
  //           " error_text=" + error_text);
  // });
}

void SoraSignaling::OnTrack(
    rtc::scoped_refptr<webrtc::RtpTransceiverInterface> transceiver) {
  boost::asio::post(*config_.io_context,
                    [self = shared_from_this(), transceiver]() {
                      auto ob = self->config_.observer.lock();
                      if (ob != nullptr) {
                        ob->OnTrack(transceiver);
                      }
                    });
}

void SoraSignaling::OnRemoveTrack(
    rtc::scoped_refptr<webrtc::RtpReceiverInterface> receiver) {
  boost::asio::post(*config_.io_context,
                    [self = shared_from_this(), receiver]() {
                      auto ob = self->config_.observer.lock();
                      if (ob != nullptr) {
                        ob->OnRemoveTrack(receiver);
                      }
                    });
}

// -----------------------------
// DataChannelObserver の実装
// -----------------------------

void SoraSignaling::OnStateChange(
    rtc::scoped_refptr<webrtc::DataChannelInterface> data_channel) {
  // まだ通知してないチャンネルが開いてた場合は通知を送る
  auto ob = config_.observer.lock();
  if (ob != nullptr) {
    for (auto& kv : dc_labels_) {
      if (kv.first[0] == '#' && !kv.second.notified && dc_->IsOpen(kv.first)) {
        ob->OnDataChannel(kv.first);
        kv.second.notified = true;
      }
    }
  }
}
void SoraSignaling::OnMessage(
    rtc::scoped_refptr<webrtc::DataChannelInterface> data_channel,
    const webrtc::DataBuffer& buffer) {
  if (!dc_) {
    return;
  }

  std::string label = data_channel->label();
  auto it = dc_labels_.find(label);
  bool compressed = it != dc_labels_.end() && it->second.compressed;
  std::string data;
  if (compressed) {
    data = ZlibHelper::Uncompress(buffer.data.cdata(), buffer.size());
  } else {
    data.assign((const char*)buffer.data.cdata(),
                (const char*)buffer.data.cdata() + buffer.size());
  }
  RTC_LOG(LS_INFO) << "label=" << label << " data=" << data;

  // ハンドリングする必要のあるラベル以外は何もしない
  if (label != "signaling" && label != "stats" && label != "push" &&
      label != "notify" && (label.empty() || label[0] != '#')) {
    return;
  }

  // ユーザ定義のラベルは JSON ではないので JSON パース前に処理して終わる
  if (!label.empty() && label[0] == '#') {
    auto ob = config_.observer.lock();
    if (ob != nullptr) {
      ob->OnMessage(std::move(label), std::move(data));
    }
    return;
  }

  boost::json::error_code ec;
  auto json = boost::json::parse(data, ec);
  if (ec) {
    RTC_LOG(LS_ERROR) << "JSON Parse Error ec=" << ec.message();
    return;
  }

  if (label == "signaling") {
    SendOnSignalingMessage(SoraSignalingType::DATACHANNEL,
                           SoraSignalingDirection::RECEIVED, std::move(data));

    const std::string type = json.at("type").as_string().c_str();
    if (type == "re-offer") {
      const std::string sdp = json.at("sdp").as_string().c_str();
      if (!CheckSdp(sdp)) {
        return;
      }

      SessionDescription::SetOffer(
          pc_.get(), sdp,
          [self = shared_from_this()]() {
            boost::asio::post(*self->config_.io_context, [self]() {
              if (self->state_ != State::Connected) {
                return;
              }

              // エンコーディングパラメータの情報がクリアされるので設定し直す
              if (self->offer_config_.simulcast) {
                self->ResetEncodingParameters();
              }

              SessionDescription::CreateAnswer(
                  self->pc_.get(),
                  [self](webrtc::SessionDescriptionInterface* desc) {
                    std::string sdp;
                    desc->ToString(&sdp);
                    boost::asio::post(*self->config_.io_context, [self, sdp]() {
                      if (self->state_ != State::Connected) {
                        return;
                      }
                      self->DoSendUpdate(sdp, "re-answer");
                    });
                  },
                  self->CreateIceError("Failed to CreateAnswer in re-offer "
                                       "message via DataChannel"));
            });
          },
          CreateIceError(
              "Failed to SetOffer in re-offer message via DataChannel"));
    }
    return;
  }

  if (label == "stats") {
    const std::string type = json.at("type").as_string().c_str();
    if (type == "req-stats") {
      pc_->GetStats(
          RTCStatsCallback::Create(
              [self = shared_from_this()](
                  const rtc::scoped_refptr<const webrtc::RTCStatsReport>&
                      report) {
                if (self->state_ != State::Connected) {
                  return;
                }
                self->DoSendPong(report);
              })
              .get());
    }
    return;
  }

  if (label == "notify") {
    auto ob = config_.observer.lock();
    if (ob != nullptr) {
      ob->OnNotify(std::move(data));
    }
    return;
  }

  if (label == "push") {
    auto ob = config_.observer.lock();
    if (ob != nullptr) {
      ob->OnPush(std::move(data));
    }
    return;
  }
}

}  // namespace sora
