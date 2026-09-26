// DataChannel の送受信を確認するテスト
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <memory>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <vector>

// Boost
#include <boost/asio/executor_work_guard.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/signal_set.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/system/detail/error_code.hpp>

// WebRTC
#include <api/peer_connection_interface.h>
#include <api/rtp_receiver_interface.h>
#include <api/rtp_transceiver_interface.h>
#include <api/scoped_refptr.h>
#include <rtc_base/logging.h>

#ifdef _WIN32
#include <rtc_base/win/scoped_com_initializer.h>
#endif

// Sora C++ SDK
#include <sora/boost_json_iwyu.h>
#include <sora/sora_client_context.h>
#include <sora/sora_signaling.h>

struct SoraClientConfig {
  std::vector<std::string> signaling_urls;
  std::string channel_id;
  std::string role;
};

class SoraClient : public std::enable_shared_from_this<SoraClient>,
                   public sora::SoraSignalingObserver {
 public:
  SoraClient(std::shared_ptr<sora::SoraClientContext> context,
             SoraClientConfig config)
      : context_(context), config_(config) {}
  ~SoraClient() {
    RTC_LOG(LS_INFO) << "SoraClient dtor";
    timer_.reset();
    ioc_.reset();
  }

  void Run() {
    ioc_.reset(new boost::asio::io_context(1));

    sora::SoraSignalingConfig config;
    config.pc_factory = pc_factory();
    config.io_context = ioc_.get();
    config.observer = shared_from_this();
    config.signaling_urls = config_.signaling_urls;
    config.channel_id = config_.channel_id;
    config.role = config_.role;
    config.video = false;
    config.audio = false;
    config.data_channel_signaling = true;
    config.multistream = true;
    std::vector<std::string> labels = {"#test1", "#test2", "#test3"};
    for (auto label : labels) {
      sora::SoraSignalingConfig::DataChannel dc;
      dc.label = label;
      dc.direction = "sendrecv";
      config.data_channels.push_back(dc);
    }
    conn_ = sora::SoraSignaling::Create(config);

    // rpc ラベルが開いていない状態では SendRpc() は送信しない
    if (conn_->SendRpc(1, "9999.0.0/not_exist", std::nullopt)) {
      RTC_LOG(LS_ERROR)
          << "Unexpectedly succeeded to SendRpc before connecting";
      std::exit(1);
    }
    // params に Object でも Array でもない値を指定した場合は送信しない
    if (conn_->SendRpc(1, "9999.0.0/not_exist", boost::json::value(1))) {
      RTC_LOG(LS_ERROR)
          << "Unexpectedly succeeded to SendRpc with invalid params";
      std::exit(1);
    }

    boost::asio::executor_work_guard<boost::asio::io_context::executor_type>
        work_guard(ioc_->get_executor());

    boost::asio::signal_set signals(*ioc_, SIGINT, SIGTERM);
    signals.async_wait(
        [this](const boost::system::error_code&, int) { conn_->Disconnect(); });

    timer_.reset(new boost::asio::steady_timer(*ioc_));
    timer_->expires_after(std::chrono::seconds(3));
    timer_->async_wait([this, labels](boost::system::error_code ec) {
      if (ec) {
        return;
      }
      // 停止前にすべてのラベルが揃っているか確認
      for (auto label : labels) {
        if (opened_.find(label) == opened_.end()) {
          RTC_LOG(LS_ERROR) << "Label was not opened: label=" << label;
          std::exit(1);
        }
      }

      // offer に含まれないラベルには送信できない
      if (conn_->SendDataChannel("#not-in-offer", "test")) {
        RTC_LOG(LS_ERROR) << "Unexpectedly succeeded to SendDataChannel: "
                             "label=#not-in-offer";
        std::exit(1);
      }

      ok_ = true;

      conn_->Disconnect();
    });

    conn_->Connect();
    ioc_->run();

    // RPC が有効な Sora に接続した場合は、SendRpc() のレスポンスが OnRpc() に届く
    if (rpc_opened_ && !rpc_received_) {
      RTC_LOG(LS_ERROR) << "RPC response was not received";
      std::exit(1);
    }
  }

  void OnSetOffer(std::string offer) override {}
  void OnDisconnect(sora::SoraSignalingErrorCode ec,
                    std::string message) override {
    RTC_LOG(LS_INFO) << "OnDisconnect: " << message;
    if (!ok_) {
      RTC_LOG(LS_ERROR) << "Unexpected error occurred: message=" << message;
      std::exit(1);
    }
    ioc_->stop();
  }
  void OnNotify(std::string text) override {}
  void OnPush(std::string text) override {}
  void OnMessage(std::string label, std::string data) override {}
  void OnRpc(std::string data) override {
    // SendRpc() で送った JSON-RPC 2.0 のリクエストに対するレスポンスが届く
    RTC_LOG(LS_INFO) << "OnRpc: " << data;
    rpc_received_ = true;
  }
  void OnSwitched(std::string text) override {
    RTC_LOG(LS_INFO) << "OnSwitched: " << text;
  }

  void OnTrack(webrtc::scoped_refptr<webrtc::RtpTransceiverInterface>
                   transceiver) override {}
  void OnRemoveTrack(
      webrtc::scoped_refptr<webrtc::RtpReceiverInterface> receiver) override {}

  void OnDataChannel(std::string label) override {
    // # で始まるラベルはユーザー定義ラベル
    if (!label.empty() && label[0] == '#') {
      // ユーザー定義ラベルには送信できる
      bool result = conn_->SendDataChannel(label, label);
      if (!result) {
        RTC_LOG(LS_ERROR) << "Failed to SendDataChannel: label=" << label;
        std::exit(1);
      }
    } else {
      // 非ユーザー定義ラベルへの SendDataChannel() は失敗する
      bool result = conn_->SendDataChannel(label, label);
      if (result) {
        RTC_LOG(LS_ERROR) << "Unexpectedly succeeded to SendDataChannel: label="
                          << label;
        std::exit(1);
      }
      // rpc ラベルが開いている場合は SendRpc() で JSON-RPC 2.0 のリクエストを
      // 送信できる。存在しないメソッドを指定して、Sora からのエラーレスポンスが
      // OnRpc() に届くことを確認する
      if (label == "rpc") {
        if (!conn_->SendRpc(1, "9999.0.0/not_exist",
                            boost::json::object{{"key", "value"}})) {
          RTC_LOG(LS_ERROR) << "Failed to SendRpc: label=" << label;
          std::exit(1);
        }
        rpc_opened_ = true;
      }
    }
    auto it = opened_.find(label);
    if (it != opened_.end()) {
      // 一度やってきたラベルが再度開くのはおかしい
      RTC_LOG(LS_ERROR) << "Unexpected re-open: label=" << label;
      std::exit(1);
    }
    opened_.insert(label);
  }

 private:
  webrtc::scoped_refptr<webrtc::PeerConnectionFactoryInterface> pc_factory() {
    return context_->peer_connection_factory();
  }

 private:
  std::shared_ptr<sora::SoraClientContext> context_;
  SoraClientConfig config_;
  std::shared_ptr<sora::SoraSignaling> conn_;
  std::unique_ptr<boost::asio::io_context> ioc_;
  std::unique_ptr<boost::asio::steady_timer> timer_;
  std::set<std::string> opened_;
  bool ok_ = false;
  // rpc ラベルが開いて SendRpc() を送信したかどうか
  bool rpc_opened_ = false;
  // SendRpc() のレスポンスを OnRpc() で受信したかどうか
  bool rpc_received_ = false;
};

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

  webrtc::LogMessage::LogToDebug(webrtc::LS_ERROR);
  webrtc::LogMessage::LogTimestamps();
  webrtc::LogMessage::LogThreads();

  sora::SoraClientContextConfig context_config;
  context_config.use_audio_device = false;
  auto context = sora::SoraClientContext::Create(context_config);

  boost::json::value v;
  {
    std::ifstream ifs(argv[1]);
    std::ostringstream oss;
    oss << ifs.rdbuf();
    std::string js = oss.str();
    v = boost::json::parse(js);
  }
  SoraClientConfig config;
  for (auto&& x : v.as_object().at("signaling_urls").as_array()) {
    config.signaling_urls.push_back(x.as_string().c_str());
  }
  config.channel_id = v.as_object().at("channel_id").as_string().c_str();
  config.role = "sendrecv";

  for (int i = 0; i < 10; i++) {
    auto client = std::make_shared<SoraClient>(context, config);
    client->Run();
  }
}
