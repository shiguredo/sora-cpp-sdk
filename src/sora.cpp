#include <rtc_base/logging.h>
#include <stdlib.h>
#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ssl/context.hpp>
#include <boost/asio/ssl/stream.hpp>
#include <boost/asio/strand.hpp>
#include <boost/beast/core/buffers_to_string.hpp>
#include <boost/beast/core/flat_buffer.hpp>
#include <boost/beast/core/multi_buffer.hpp>
#include <boost/beast/websocket/ssl.hpp>
#include <boost/beast/websocket/stream.hpp>
#include <rotor.hpp>
#include <rotor/asio.hpp>

#include "ssl_verifier.h"
#include "url_parts.h"

/*
struct server_actor : public rotor::actor_base_t {
  using rotor::actor_base_t::actor_base_t;
  void on_start() noexcept override {
    rotor::actor_base_t::on_start();
    RTC_LOG(LS_INFO) << "hello world";
    supervisor->shutdown();
  }
};

class RTCManager {
 public:
};

struct WebsocketConfig : public rotor::actor_config_t {
  rotor::address_ptr_t receiver;
};

template <class Actor>
struct WebsocketConfigBuilder : public rotor::actor_config_builder_t<Actor> {
  using builder_t = typename Actor::template config_builder_t<Actor>;
  using parent_t = rotor::actor_config_builder_t<Actor>;
  using parent_t::parent_t;

  builder_t&& receiver(const rotor::address_ptr_t& value) && noexcept {
    config.receiver = value;
    return std::move(*this);
  }
};

class Websocket : public rotor::actor_base_t {
 public:
  enum MessageType {
    MSG_CONNECT,
    MSG_READ,
    MSG_WRITE,
    MSG_CLOSE,
  };
  struct Message {
    rotor::address_ptr_t sender;
    MessageType type;
    boost::system::error_code ec;
    std::string data;
  };

  using config_t = WebsocketConfig;
  template <class Actor>
  using config_builder_t = WebsocketConfigBuilder<Actor>;

  Websocket(config_t& config)
      : rotor::actor_base_t(config),
        receiver_(config.receiver),
        strand_(static_cast<rotor::asio::supervisor_asio_t*>(config.supervisor)
                    ->get_strand()) {
    RTC_LOG(LS_INFO) << "Websocket Created";
  }

 private:
  struct OpConnect {
    std::string url;
    bool insecure;
    boost::asio::ssl::context ssl_ctx;
  };
  struct OpWrite {
    std::string buf;
  };
  struct OpClose {};

 public:
  static boost::asio::ssl::context CreateSSLContext() {
    boost::asio::ssl::context ctx(boost::asio::ssl::context::tlsv12);
    //ctx.set_default_verify_paths();
    ctx.set_options(boost::asio::ssl::context::default_workarounds |
                    boost::asio::ssl::context::no_sslv2 |
                    boost::asio::ssl::context::no_sslv3 |
                    boost::asio::ssl::context::single_dh_use);
    return ctx;
  }

  static void Connect(rotor::actor_base_t& actor,
                      rotor::address_ptr_t ws,
                      std::string url) {
    RTC_LOG(LS_INFO) << "Websocket::Connect ws=" << ws << " url=" << url;
    actor.send<OpConnect>(ws, std::move(url), false, CreateSSLContext());
  }
  static void Connect(rotor::actor_base_t& actor,
                      rotor::address_ptr_t ws,
                      std::string url,
                      bool insecure,
                      boost::asio::ssl::context ssl_ctx) {
    actor.send<OpConnect>(ws, std::move(url), insecure, std::move(ssl_ctx));
  }
  static void Write(rotor::actor_base_t& actor,
                    rotor::address_ptr_t ws,
                    std::string buf) {
    actor.send<OpWrite>(ws, std::move(buf));
  }
  static void Close(rotor::actor_base_t& actor, rotor::address_ptr_t ws) {
    actor.send<OpClose>(ws);
  }

 private:
  void SendMessage(MessageType type,
                   boost::system::error_code ec,
                   std::string data = "") {
    supervisor->send<Message>(receiver_, get_address(), type, ec,
                              std::move(data));
    supervisor->do_process();
  }

  using self_ptr_t = rotor::intrusive_ptr_t<Websocket>;
  void OnOpConnect(rotor::message_t<OpConnect>& message) noexcept {
    RTC_LOG(LS_INFO) << "Recv OnOpConnect";
    resolver_.reset(new boost::asio::ip::tcp::resolver(strand_.context()));

    auto& cfg = message.payload;
    if (!URLParts::Parse(cfg.url, parts_)) {
      SendMessage(MSG_CONNECT, boost::system::errc::make_error_code(
                                   boost::system::errc::invalid_argument));
      return;
    }

    // wss と ws のみサポート
    if (parts_.scheme != "wss" && parts_.scheme != "ws") {
      SendMessage(MSG_CONNECT, boost::system::errc::make_error_code(
                                   boost::system::errc::invalid_argument));
      return;
    }

    if (parts_.scheme == "wss") {
      wss_.reset(new ssl_websocket_t(strand_.context(), cfg.ssl_ctx));
      wss_->write_buffer_bytes(8192);

      wss_->next_layer().set_verify_mode(boost::asio::ssl::verify_peer);
      wss_->next_layer().set_verify_callback(
          [insecure = cfg.insecure](bool preverified,
                                    boost::asio::ssl::verify_context& ctx) {
            if (preverified) {
              return true;
            }
            // insecure の場合は証明書をチェックしない
            if (insecure) {
              return true;
            }
            X509* cert = X509_STORE_CTX_get0_cert(ctx.native_handle());
            STACK_OF(X509)* chain =
                X509_STORE_CTX_get0_chain(ctx.native_handle());
            return SSLVerifier::VerifyX509(cert, chain);
          });

      // SNI の設定を行う
      if (!SSL_set_tlsext_host_name(wss_->next_layer().native_handle(),
                                    parts_.host.c_str())) {
        boost::system::error_code ec{static_cast<int>(::ERR_get_error()),
                                     boost::asio::error::get_ssl_category()};
        supervisor->send<Message>(receiver_, get_address(), MSG_CONNECT,
                                  boost::system::errc::make_error_code(
                                      boost::system::errc::invalid_argument));
        return;
      }
    } else {
      ws_.reset(new websocket_t(strand_.context()));
      ws_->write_buffer_bytes(8192);
    }

    std::string port;
    if (parts_.port.empty()) {
      port = parts_.scheme == "wss" ? "443" : "80";
    } else {
      port = parts_.port;
    }

    // DNS ルックアップ
    resolver_->async_resolve(
        parts_.host, port,
        std::bind(&Websocket::OnResolve, self_ptr_t(this),
                  std::placeholders::_1, std::placeholders::_2));
  }
  void OnResolve(boost::system::error_code ec,
                 boost::asio::ip::tcp::resolver::results_type results) {
    if (ec) {
      SendMessage(MSG_CONNECT, ec);
      return;
    }

    // DNS ルックアップで得られたエンドポイントに対して接続する
    if (wss_ != nullptr) {
      boost::asio::async_connect(
          wss_->next_layer().next_layer(), results.begin(), results.end(),
          std::bind(&Websocket::OnSSLConnect, self_ptr_t(this),
                    std::placeholders::_1));
    } else {
      boost::asio::async_connect(
          ws_->next_layer(), results.begin(), results.end(),
          std::bind(&Websocket::OnConnect, self_ptr_t(this),
                    std::placeholders::_1));
    }
  }

  void OnSSLConnect(boost::system::error_code ec) {
    if (ec) {
      SendMessage(MSG_CONNECT, ec);
      return;
    }

    // SSL のハンドシェイク
    wss_->next_layer().async_handshake(
        boost::asio::ssl::stream_base::client,
        std::bind(&Websocket::OnSSLHandshake, self_ptr_t(this),
                  std::placeholders::_1));
  }

  void OnSSLHandshake(boost::system::error_code ec) {
    if (ec) {
      SendMessage(MSG_CONNECT, ec);
      return;
    }

    // Websocket のハンドシェイク
    wss_->async_handshake(parts_.host, parts_.path_query_fragment,
                          std::bind(&Websocket::OnHandshake, self_ptr_t(this),
                                    std::placeholders::_1));
  }

  void OnConnect(boost::system::error_code ec) {
    if (ec) {
      SendMessage(MSG_CONNECT, ec);
      return;
    }

    // Websocket のハンドシェイク
    ws_->async_handshake(parts_.host, parts_.path_query_fragment,
                         std::bind(&Websocket::OnHandshake, self_ptr_t(this),
                                   std::placeholders::_1));
  }

  void OnHandshake(boost::system::error_code ec) {
    if (ec) {
      SendMessage(MSG_CONNECT, ec);
      return;
    }

    SendMessage(MSG_CONNECT, ec);
    if (wss_ != nullptr) {
      wss_->async_read(read_buffer_,
                       std::bind(&Websocket::OnRead, self_ptr_t(this),
                                 std::placeholders::_1, std::placeholders::_2));
    } else {
      ws_->async_read(read_buffer_,
                      std::bind(&Websocket::OnRead, self_ptr_t(this),
                                std::placeholders::_1, std::placeholders::_2));
    }
  }

  void OnRead(boost::system::error_code ec, std::size_t bytes_transferred) {
    if (ec) {
      RTC_LOG(LS_ERROR) << __FUNCTION__ << ": " << ec.message();
      SendMessage(MSG_READ, ec);
      return;
    }

    //RTC_LOG(LS_INFO) << "Websocket::OnRead this=" << (void*)this
    //                 << " ec=" << ec.message();

    const auto text = boost::beast::buffers_to_string(read_buffer_.data());
    read_buffer_.consume(read_buffer_.size());

    SendMessage(MSG_READ, ec, std::move(text));
  }
  void OnOpWrite(rotor::message_t<OpWrite>& message) noexcept {
    const auto& cfg = message.payload;
    bool empty = write_data_.empty();
    boost::beast::flat_buffer buffer;

    const auto n = boost::asio::buffer_copy(buffer.prepare(cfg.buf.size()),
                                            boost::asio::buffer(cfg.buf));
    buffer.commit(n);

    write_data_.emplace_back(new WriteData{std::move(buffer), true});

    if (empty) {
      DoWrite();
    }
  }
  void DoWrite() {
    auto& data = write_data_.front();

    //RTC_LOG(LS_VERBOSE) << __FUNCTION__ << ": "
    //                    << boost::beast::buffers_to_string(data->buffer.data());

    if (wss_ != nullptr) {
      wss_->text(data->text);
      wss_->async_write(
          data->buffer.data(),
          std::bind(&Websocket::OnWrite, self_ptr_t(this),
                    std::placeholders::_1, std::placeholders::_2));
    } else {
      ws_->text(data->text);
      ws_->async_write(data->buffer.data(),
                       std::bind(&Websocket::OnWrite, self_ptr_t(this),
                                 std::placeholders::_1, std::placeholders::_2));
    }
  }

  void Websocket::OnWrite(boost::system::error_code ec,
                          std::size_t bytes_transferred) {
    //RTC_LOG(LS_INFO) << "Websocket::OnWrite this=" << (void*)this
    //                 << " ec=" << ec.message();

    if (ec) {
      RTC_LOG(LS_ERROR) << __FUNCTION__ << ": " << ec.message();
      SendMessage(MSG_WRITE, ec);
      return;
    }

    SendMessage(MSG_WRITE, ec);
    write_data_.erase(write_data_.begin());
    if (!write_data_.empty()) {
      DoWrite();
    }
  }

  void OnOpClose(rotor::message_t<OpClose>& message) noexcept {
    if (wss_ != nullptr) {
      wss_->async_close(boost::beast::websocket::close_code::normal,
                        std::bind(&Websocket::OnClose, self_ptr_t(this),
                                  std::placeholders::_1));
    } else {
      ws_->async_close(boost::beast::websocket::close_code::normal,
                       std::bind(&Websocket::OnClose, self_ptr_t(this),
                                 std::placeholders::_1));
    }
  }
  void OnClose(boost::system::error_code ec) noexcept {
    SendMessage(MSG_CLOSE, ec);
  }

 public:
  void on_start() noexcept override {
    rotor::actor_base_t::on_start();
    RTC_LOG(LS_INFO) << "Websocket on_start address=" << get_address();
  }
  void configure(rotor::plugin::plugin_base_t& plugin) noexcept override {
    plugin.with_casted<rotor::plugin::starter_plugin_t>([&](auto& p) {
      RTC_LOG(LS_INFO) << "Websocket configure";
      p.subscribe_actor(&Websocket::OnOpConnect);
      p.subscribe_actor(&Websocket::OnOpWrite);
      p.subscribe_actor(&Websocket::OnOpClose);
    });
  }

 private:
  rotor::address_ptr_t receiver_;
  boost::asio::io_context::strand strand_;

  typedef boost::beast::websocket::stream<boost::asio::ip::tcp::socket>
      websocket_t;
  typedef boost::beast::websocket::stream<
      boost::asio::ssl::stream<boost::asio::ip::tcp::socket>>
      ssl_websocket_t;

  std::unique_ptr<websocket_t> ws_;
  std::unique_ptr<ssl_websocket_t> wss_;

  std::unique_ptr<boost::asio::ip::tcp::resolver> resolver_;
  URLParts parts_;

  boost::beast::multi_buffer read_buffer_;
  struct WriteData {
    boost::beast::flat_buffer buffer;
    bool text;
  };
  std::vector<std::unique_ptr<WriteData>> write_data_;
};

class Sora : public rotor::actor_base_t {
 public:
  using rotor::actor_base_t::actor_base_t;

  void configure(rotor::plugin::plugin_base_t& plugin) noexcept override {
    plugin.with_casted<rotor::plugin::starter_plugin_t>(
        [&](rotor::plugin::starter_plugin_t& p) {
          p.subscribe_actor(&Sora::OnWebsocket);
        });
  }
  void on_start() noexcept override {
    auto timeout = boost::posix_time::milliseconds{500};
    auto actor = supervisor->create_actor<Websocket>()
                     .timeout(timeout)
                     .receiver(get_address())
                     .finish();
    ws_ = actor->get_address();

    RTC_LOG(LS_INFO) << "Connect Started";
    Websocket::Connect(*this, ws_, "wss://sora-test.shiguredo.jp");
  }

  void OnWebsocket(rotor::message_t<Websocket::Message>& message) noexcept {
    const auto& msg = message.payload;
    if (msg.sender != ws_) {
      return;
    }
    switch (msg.type) {
      case Websocket::MSG_CONNECT:
        RTC_LOG(LS_INFO) << "OnConnect";
        break;
    }
  }

 private:
  rotor::address_ptr_t ws_;
};

extern "C" {
void sora_run() {
  putenv("ROTOR_INSPECT_DELIVERY=2");

  rtc::LogMessage::LogToDebug(rtc::LS_INFO);
  rtc::LogMessage::LogTimestamps();
  rtc::LogMessage::LogThreads();

  boost::asio::io_context ioc;
  auto system_context = rotor::asio::system_context_asio_t::ptr_t{
      new rotor::asio::system_context_asio_t(ioc)};
  auto strand = std::make_shared<boost::asio::io_context::strand>(ioc);
  auto timeout = boost::posix_time::milliseconds{500};
  auto sup = system_context->create_supervisor<rotor::asio::supervisor_asio_t>()
                 .strand(strand)
                 .timeout(timeout)
                 .guard_context(true)
                 .finish();

  auto sora = sup->create_actor<Sora>().timeout(timeout).finish();

  RTC_LOG(LS_INFO) << "hoge-";
  sup->start();
  ioc.run();
  RTC_LOG(LS_INFO) << "fuga-";
}
}
*/

#include "actor.h"
#include "future.h"

extern "C" {
void sora_run() {
  test_future();
  test_process();
}
}