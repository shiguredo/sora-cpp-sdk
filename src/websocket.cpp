#include "sora/websocket.h"

#include <utility>

// WebRTC
#include <rtc_base/logging.h>

#include "absl/strings/escaping.h"

// Boost
#include <boost/asio/bind_executor.hpp>
#include <boost/asio/connect.hpp>
#include <boost/asio/post.hpp>
#include <boost/beast/core/buffers_to_string.hpp>
#include <boost/beast/websocket/stream.hpp>

#include "sora/ssl_verifier.h"
#include "sora/version.h"

namespace sora {

static std::shared_ptr<boost::asio::ssl::context> CreateSSLContext(
    const std::optional<std::string>& client_cert,
    const std::optional<std::string>& client_key) {
  // TLS 1.2 と 1.3 のみ対応
  SSL_CTX* handle = ::SSL_CTX_new(::TLS_method());
  SSL_CTX_set_min_proto_version(handle, TLS1_2_VERSION);
  SSL_CTX_set_max_proto_version(handle, TLS1_3_VERSION);
  auto ctx = std::make_shared<boost::asio::ssl::context>(handle);
  //ctx.set_default_verify_paths();
  ctx->set_options(boost::asio::ssl::context::default_workarounds |
                   boost::asio::ssl::context::no_sslv2 |
                   boost::asio::ssl::context::no_sslv3 |
                   boost::asio::ssl::context::single_dh_use);
  if (client_cert) {
    boost::system::error_code ec;
    ctx->use_certificate(boost::asio::buffer(*client_cert),
                         boost::asio::ssl::context_base::file_format::pem, ec);
    if (ec) {
      RTC_LOG(LS_WARNING) << "client_cert is set, but use_certificate failed: "
                          << ec.message();
    } else {
      RTC_LOG(LS_INFO) << "client_cert is set";
    }
  }
  if (client_key) {
    boost::system::error_code ec;
    ctx->use_private_key(boost::asio::buffer(*client_key),
                         boost::asio::ssl::context_base::file_format::pem, ec);
    if (ec) {
      RTC_LOG(LS_WARNING) << "client_key is set, but use_private_key failed: "
                          << ec.message();
    } else {
      RTC_LOG(LS_INFO) << "client_key is set";
    }
  }
  return ctx;
}

Websocket::Websocket(boost::asio::io_context& ioc)
    : ws_(new websocket_t(ioc)),
      resolver_(new boost::asio::ip::tcp::resolver(ioc)),
      strand_(ws_->get_executor()),
      close_timeout_timer_(ioc),
      user_agent_(Version::GetDefaultUserAgent()) {
  ws_->write_buffer_bytes(8192);
}
Websocket::Websocket(Websocket::ssl_tag,
                     boost::asio::io_context& ioc,
                     bool insecure,
                     const std::optional<std::string>& client_cert,
                     const std::optional<std::string>& client_key,
                     const std::optional<std::string>& ca_cert)
    : resolver_(new boost::asio::ip::tcp::resolver(ioc)),
      strand_(ioc.get_executor()),
      close_timeout_timer_(ioc),
      insecure_(insecure),
      user_agent_(Version::GetDefaultUserAgent()),
      ca_cert_(ca_cert) {
  ssl_ctx_ = CreateSSLContext(client_cert, client_key);
  wss_.reset(new ssl_websocket_t(ioc, *ssl_ctx_));
  InitWss(wss_.get(), insecure, ca_cert);
}
Websocket::Websocket(boost::asio::ip::tcp::socket socket)
    : ws_(new websocket_t(std::move(socket))),
      strand_(ws_->get_executor()),
      close_timeout_timer_(ws_->get_executor()),
      user_agent_(Version::GetDefaultUserAgent()) {
  ws_->write_buffer_bytes(8192);
}
Websocket::Websocket(https_proxy_tag,
                     boost::asio::io_context& ioc,
                     bool insecure,
                     const std::optional<std::string>& client_cert,
                     const std::optional<std::string>& client_key,
                     const std::optional<std::string>& ca_cert,
                     std::string proxy_url,
                     std::string proxy_username,
                     std::string proxy_password)
    : resolver_(new boost::asio::ip::tcp::resolver(ioc)),
      strand_(ioc.get_executor()),
      close_timeout_timer_(ioc),
      insecure_(insecure),
      ca_cert_(ca_cert),
      https_proxy_(true),
      proxy_socket_(new boost::asio::ip::tcp::socket(ioc)),
      proxy_url_(std::move(proxy_url)),
      proxy_username_(std::move(proxy_username)),
      proxy_password_(std::move(proxy_password)),
      user_agent_(Version::GetDefaultUserAgent()) {
  ssl_ctx_ = CreateSSLContext(client_cert, client_key);
}

Websocket::~Websocket() {
  RTC_LOG(LS_INFO) << "Websocket::~Websocket this=" << (void*)this;
}

void Websocket::SetUserAgent(http_header_value user_agent) {
  user_agent_ = user_agent;
}

bool Websocket::IsSSL() const {
  return https_proxy_ || wss_ != nullptr;
}

void Websocket::InitWss(ssl_websocket_t* wss,
                        bool insecure,
                        const std::optional<std::string>& ca_cert) {
  wss->write_buffer_bytes(8192);

  wss->next_layer().set_verify_mode(boost::asio::ssl::verify_peer);
  wss->next_layer().set_verify_callback(
      [insecure, ca_cert](bool preverified,
                          boost::asio::ssl::verify_context& ctx) {
        if (preverified) {
          return true;
        }
        // insecure の場合は証明書をチェックしない
        if (insecure) {
          return true;
        }
        X509* cert = X509_STORE_CTX_get0_cert(ctx.native_handle());
        STACK_OF(X509)* chain = X509_STORE_CTX_get0_chain(ctx.native_handle());
        return SSLVerifier::VerifyX509(cert, chain, ca_cert);
      });
}

Websocket::websocket_t& Websocket::NativeSocket() {
  return *ws_;
}

Websocket::ssl_websocket_t& Websocket::NativeSecureSocket() {
  return *wss_;
}

void Websocket::Connect(const std::string& url, connect_callback_t on_connect) {
  // proxy 時は wss のみサポート
  if (https_proxy_) {
    ConnectProxy(url, std::move(on_connect));
    return;
  }

  if (!URLParts::Parse(url, parts_)) {
    on_connect(boost::system::errc::make_error_code(
        boost::system::errc::invalid_argument));
    return;
  }

  // wss と ws のみサポート
  if (parts_.scheme != "wss" && parts_.scheme != "ws") {
    on_connect(boost::system::errc::make_error_code(
        boost::system::errc::invalid_argument));
    return;
  }

  // コンストラクタの設定と接続のスキームが合っているか
  if (IsSSL() && parts_.scheme != "wss" || !IsSSL() && parts_.scheme != "ws") {
    on_connect(boost::system::errc::make_error_code(
        boost::system::errc::invalid_argument));
    return;
  }

  if (IsSSL()) {
    // SNI の設定を行う
    if (!SSL_set_tlsext_host_name(wss_->next_layer().native_handle(),
                                  parts_.host.c_str())) {
      boost::system::error_code ec{static_cast<int>(::ERR_get_error()),
                                   boost::asio::error::get_ssl_category()};
      on_connect(ec);
      return;
    }
  }

  // ヘッダーの設定
  auto set_headers = [this](boost::beast::websocket::request_type& req) {
    if (user_agent_ != std::nullopt) {
      req.set(boost::beast::http::field::user_agent, *user_agent_);
    }
  };
  if (IsSSL()) {
    wss_->set_option(
        boost::beast::websocket::stream_base::decorator(set_headers));
  } else {
    ws_->set_option(
        boost::beast::websocket::stream_base::decorator(set_headers));
  }

  on_connect_ = std::move(on_connect);

  // DNS ルックアップ
  resolver_->async_resolve(
      parts_.host, parts_.GetPort(),
      std::bind(&Websocket::OnResolve, this, parts_.host, parts_.GetPort(),
                std::placeholders::_1, std::placeholders::_2));
}

void Websocket::OnResolve(
    std::string host,
    std::string port,
    boost::system::error_code ec,
    boost::asio::ip::tcp::resolver::results_type results) {
  if (ec) {
    auto on_connect = std::move(on_connect_);
    on_connect(ec);
    return;
  }

  for (const auto& r : results) {
    RTC_LOG(LS_INFO) << "host=" << host << ":" << port
                     << " resolved=" << r.endpoint();
  }

  // DNS ルックアップで得られたエンドポイントに対して接続する
  if (IsSSL()) {
    boost::asio::async_connect(
        wss_->next_layer().next_layer(), results.begin(), results.end(),
        std::bind(&Websocket::OnSSLConnect, this, std::placeholders::_1));
  } else {
    boost::asio::async_connect(
        ws_->next_layer(), results.begin(), results.end(),
        std::bind(&Websocket::OnConnect, this, std::placeholders::_1));
  }
}

void Websocket::OnSSLConnect(boost::system::error_code ec) {
  if (ec) {
    auto on_connect = std::move(on_connect_);
    on_connect(ec);
    return;
  }

  // SSL のハンドシェイク
  wss_->next_layer().async_handshake(
      boost::asio::ssl::stream_base::client,
      std::bind(&Websocket::OnSSLHandshake, this, std::placeholders::_1));
}

void Websocket::OnSSLHandshake(boost::system::error_code ec) {
  if (ec) {
    auto on_connect = std::move(on_connect_);
    on_connect(ec);
    return;
  }

  // Websocket のハンドシェイク
  wss_->async_handshake(
      parts_.host, parts_.path_query_fragment,
      std::bind(&Websocket::OnHandshake, this, std::placeholders::_1));
}

void Websocket::OnConnect(boost::system::error_code ec) {
  if (ec) {
    auto on_connect = std::move(on_connect_);
    on_connect(ec);
    return;
  }

  // Websocket のハンドシェイク
  ws_->async_handshake(
      parts_.host, parts_.path_query_fragment,
      std::bind(&Websocket::OnHandshake, this, std::placeholders::_1));
}

void Websocket::OnHandshake(boost::system::error_code ec) {
  auto on_connect = std::move(on_connect_);
  on_connect(ec);
}

void Websocket::Accept(
    boost::beast::http::request<boost::beast::http::string_body> req,
    connect_callback_t on_connect) {
  on_connect_ = std::move(on_connect);
  ws_->async_accept(
      req, std::bind(&Websocket::OnAccept, this, std::placeholders::_1));
}

void Websocket::OnAccept(boost::system::error_code ec) {
  auto on_connect = std::move(on_connect_);
  on_connect(ec);
}

void Websocket::Read(read_callback_t on_read) {
  boost::asio::post(strand_,
                    std::bind(&Websocket::DoRead, this, std::move(on_read)));
}

void Websocket::DoRead(read_callback_t on_read) {
  if (IsSSL()) {
    wss_->async_read(read_buffer_,
                     std::bind(&Websocket::OnRead, this, std::move(on_read),
                               std::placeholders::_1, std::placeholders::_2));
  } else {
    ws_->async_read(read_buffer_,
                    std::bind(&Websocket::OnRead, this, std::move(on_read),
                              std::placeholders::_1, std::placeholders::_2));
  }
}

void Websocket::ConnectProxy(const std::string& url,
                             connect_callback_t on_connect) {
  if (!URLParts::Parse(url, parts_)) {
    on_connect(boost::system::errc::make_error_code(
        boost::system::errc::invalid_argument));
    return;
  }

  if (parts_.scheme != "wss") {
    on_connect(boost::system::errc::make_error_code(
        boost::system::errc::invalid_argument));
    return;
  }

  if (!URLParts::Parse(proxy_url_, proxy_parts_)) {
    on_connect(boost::system::errc::make_error_code(
        boost::system::errc::invalid_argument));
    return;
  }

  if (!URLParts::Parse(url, parts_)) {
    on_connect(boost::system::errc::make_error_code(
        boost::system::errc::invalid_argument));
    return;
  }

  // 今は http + wss の組み合わせしか許可しない
  if (proxy_parts_.scheme != "http" || parts_.scheme != "wss") {
    on_connect(boost::system::errc::make_error_code(
        boost::system::errc::invalid_argument));
    return;
  }

  on_connect_ = std::move(on_connect);

  // proxy サーバーの DNS 解決を行う
  resolver_->async_resolve(
      proxy_parts_.host, proxy_parts_.GetPort(),
      std::bind(&Websocket::OnResolveProxy, this, proxy_parts_.host,
                proxy_parts_.GetPort(), std::placeholders::_1,
                std::placeholders::_2));
}

void Websocket::OnResolveProxy(
    std::string host,
    std::string port,
    boost::system::error_code ec,
    boost::asio::ip::tcp::resolver::results_type results) {
  if (ec) {
    auto on_connect = std::move(on_connect_);
    on_connect(ec);
    return;
  }

  for (const auto& r : results) {
    RTC_LOG(LS_INFO) << "host=" << host << ":" << port
                     << " resolved=" << r.endpoint();
  }

  boost::asio::async_connect(
      *proxy_socket_, results.begin(), results.end(),
      std::bind(&Websocket::OnConnectProxy, this, std::placeholders::_1));
}

void Websocket::OnConnectProxy(boost::system::error_code ec) {
  if (ec) {
    auto on_connect = std::move(on_connect_);
    on_connect(ec);
    return;
  }

  // CONNECT 送信
  auto target = parts_.host + ":" + parts_.GetPort();
  proxy_req_.method(boost::beast::http::verb::connect);
  proxy_req_.target(target);
  proxy_req_.version(11);
  proxy_req_.set(boost::beast::http::field::host, target);
  proxy_req_.set(
      boost::beast::http::field::proxy_authorization,
      "Basic " + absl::Base64Escape(proxy_username_ + ":" + proxy_password_));
  boost::beast::http::async_write(
      *proxy_socket_, proxy_req_,
      std::bind(&Websocket::OnWriteProxy, this, std::placeholders::_1,
                std::placeholders::_2));
}

void Websocket::OnWriteProxy(boost::system::error_code ec,
                             std::size_t bytes_transferred) {
  if (ec) {
    auto on_connect = std::move(on_connect_);
    on_connect(ec);
    return;
  }

  // CONNECT のレスポンス読み込み
  proxy_resp_parser_.reset(
      new boost::beast::http::response_parser<boost::beast::http::empty_body>(
          proxy_resp_));
  // see https://stackoverflow.com/a/49837467/10904212
  proxy_resp_parser_->skip(true);
  boost::beast::http::async_read(
      *proxy_socket_, proxy_buffer_, *proxy_resp_parser_,
      std::bind(&Websocket::OnReadProxy, this, std::placeholders::_1,
                std::placeholders::_2));
}

void Websocket::OnReadProxy(boost::system::error_code ec,
                            std::size_t bytes_transferred) {
  if (ec) {
    auto on_connect = std::move(on_connect_);
    on_connect(ec);
    return;
  }

  // wss を作って、あとは普通の SSL ハンドシェイクを行う
  wss_.reset(new ssl_websocket_t(std::move(*proxy_socket_), *ssl_ctx_));
  InitWss(wss_.get(), insecure_, ca_cert_);

  // SNI の設定を行う
  if (!SSL_set_tlsext_host_name(wss_->next_layer().native_handle(),
                                parts_.host.c_str())) {
    boost::system::error_code ec{static_cast<int>(::ERR_get_error()),
                                 boost::asio::error::get_ssl_category()};
    auto on_connect = std::move(on_connect_);
    on_connect(ec);
    return;
  }

  wss_->next_layer().async_handshake(
      boost::asio::ssl::stream_base::client,
      std::bind(&Websocket::OnSSLHandshake, this, std::placeholders::_1));
}

void Websocket::OnRead(read_callback_t on_read,
                       boost::system::error_code ec,
                       std::size_t bytes_transferred) {
  RTC_LOG(LS_INFO) << "Websocket::OnRead this=" << (void*)this
                   << " ec=" << ec.message();

  if (ec) {
    RTC_LOG(LS_ERROR) << __FUNCTION__ << ": " << ec.message();
  }

  std::string text;
  if (!ec) {
    text = boost::beast::buffers_to_string(read_buffer_.data());
    read_buffer_.consume(read_buffer_.size());
  }

  std::move(on_read)(ec, bytes_transferred, std::move(text));
}

void Websocket::WriteText(std::string text, write_callback_t on_write) {
  boost::asio::post(strand_, std::bind(&Websocket::DoWriteText, this,
                                       std::move(text), std::move(on_write)));
}

void Websocket::DoWriteText(std::string text, write_callback_t on_write) {
  bool empty = write_data_.empty();
  boost::beast::flat_buffer buffer;

  const auto n = boost::asio::buffer_copy(buffer.prepare(text.size()),
                                          boost::asio::buffer(text));
  buffer.commit(n);

  write_data_.emplace_back(
      new WriteData{std::move(buffer), std::move(on_write), true});

  if (empty) {
    DoWrite();
  }
}
void Websocket::DoWrite() {
  auto& data = write_data_.front();

  RTC_LOG(LS_VERBOSE) << __FUNCTION__ << ": "
                      << boost::beast::buffers_to_string(data->buffer.data());

  if (IsSSL()) {
    wss_->text(data->text);
    wss_->async_write(data->buffer.data(),
                      std::bind(&Websocket::OnWrite, this,
                                std::placeholders::_1, std::placeholders::_2));
  } else {
    ws_->text(data->text);
    ws_->async_write(data->buffer.data(),
                     std::bind(&Websocket::OnWrite, this, std::placeholders::_1,
                               std::placeholders::_2));
  }
}

void Websocket::OnWrite(boost::system::error_code ec,
                        std::size_t bytes_transferred) {
  RTC_LOG(LS_INFO) << "Websocket::OnWrite this=" << (void*)this
                   << " ec=" << ec.message();

  if (ec) {
    RTC_LOG(LS_ERROR) << __FUNCTION__ << ": " << ec.message();
  }

  if (ec == boost::asio::error::operation_aborted) {
    return;
  }

  auto& data = write_data_.front();
  if (data->callback) {
    std::move(data->callback)(ec, bytes_transferred);
  }

  write_data_.erase(write_data_.begin());

  if (!write_data_.empty()) {
    DoWrite();
  }
}

void Websocket::Close(close_callback_t on_close, int timeout_seconds) {
  boost::asio::post(strand_, std::bind(&Websocket::DoClose, this,
                                       std::move(on_close), timeout_seconds));
}

void Websocket::CloseSocket(boost::system::error_code& ec) {
  if (IsSSL()) {
    wss_->next_layer().next_layer().close(ec);
  } else {
    ws_->next_layer().close(ec);
  }
}

void Websocket::DoClose(close_callback_t on_close, int timeout_seconds) {
  if (IsSSL()) {
    RTC_LOG(LS_INFO) << "DoClose wss this=" << (void*)this;
    wss_->async_close(
        boost::beast::websocket::close_code::normal,
        std::bind(&Websocket::OnClose, this, on_close, std::placeholders::_1));
  } else {
    RTC_LOG(LS_INFO) << "DoClose ws this=" << (void*)this;
    ws_->async_close(
        boost::beast::websocket::close_code::normal,
        std::bind(&Websocket::OnClose, this, on_close, std::placeholders::_1));
  }

  close_timeout_timer_.expires_from_now(
      boost::posix_time::seconds(timeout_seconds));
  close_timeout_timer_.async_wait(
      [on_close, timeout_seconds, this](boost::system::error_code ec) {
        if (ec) {
          return;
        }
        RTC_LOG(LS_INFO) << "Websocket::Close timeout this=" << (void*)this
                         << " timeout_seconds=" << timeout_seconds;
        Cancel();
      });
}

void Websocket::OnClose(close_callback_t on_close,
                        boost::system::error_code ec) {
  RTC_LOG(LS_INFO) << "Websocket::OnClose this=" << (void*)this
                   << " ec=" << ec.message() << " code=" << reason().code
                   << " reason=" << reason().reason;
  boost::system::error_code tec;
  close_timeout_timer_.cancel(tec);
  on_close(ec);
}

void Websocket::Cancel() {
  boost::system::error_code ec;
  if (IsSSL()) {
    wss_->next_layer().next_layer().cancel(ec);
  } else {
    ws_->next_layer().cancel(ec);
  }
}

const boost::beast::websocket::close_reason& Websocket::reason() const {
  return IsSSL() ? wss_->reason() : ws_->reason();
}

}  // namespace sora
