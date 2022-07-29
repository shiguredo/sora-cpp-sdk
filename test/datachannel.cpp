// DataChannel の送受信を確認するテスト
#include <fstream>
#include <iostream>

// WebRTC
#include <rtc_base/logging.h>

#ifdef _WIN32
#include <rtc_base/win/scoped_com_initializer.h>
#endif

#include "sora/audio_device_module.h"
#include "sora/camera_device_capturer.h"
#include "sora/java_context.h"
#include "sora/sora_default_client.h"
#include "sora/sora_video_decoder_factory.h"
#include "sora/sora_video_encoder_factory.h"

struct SoraClientConfig : sora::SoraDefaultClientConfig {
  std::vector<std::string> signaling_urls;
  std::string channel_id;
  std::string role;
};

class SoraClient : public std::enable_shared_from_this<SoraClient>,
                   public sora::SoraDefaultClient {
 public:
  SoraClient(SoraClientConfig config)
      : sora::SoraDefaultClient(config), config_(config) {}
  ~SoraClient() {
    RTC_LOG(LS_INFO) << "SoraClient dtor";
    timer_.reset();
    ioc_.reset();
  }

  void Run() {
    ioc_.reset(new boost::asio::io_context(1));

    sora::SoraSignalingConfig config;
    config.pc_factory = factory();
    config.io_context = ioc_.get();
    config.observer = shared_from_this();
    config.signaling_urls = config_.signaling_urls;
    config.channel_id = config_.channel_id;
    config.sora_client = "Hello Sora";
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

    boost::asio::executor_work_guard<boost::asio::io_context::executor_type>
        work_guard(ioc_->get_executor());

    boost::asio::signal_set signals(*ioc_, SIGINT, SIGTERM);
    signals.async_wait(
        [this](const boost::system::error_code&, int) { conn_->Disconnect(); });

    timer_.reset(new boost::asio::deadline_timer(*ioc_));
    timer_->expires_from_now(boost::posix_time::seconds(1));
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
      ok_ = true;

      conn_->Disconnect();
    });

    conn_->Connect();
    ioc_->run();
  }

  void OnDisconnect(sora::SoraSignalingErrorCode ec,
                    std::string message) override {
    RTC_LOG(LS_INFO) << "OnDisconnect: " << message;
    if (!ok_) {
      RTC_LOG(LS_ERROR) << "Unexpected error occurred: message=" << message;
      std::exit(1);
    }
    ioc_->stop();
  }

  void OnDataChannel(std::string label) override {
    bool result = conn_->SendDataChannel(label, label);
    if (!result) {
      RTC_LOG(LS_ERROR) << "Failed to SendDataChannel: label=" << label;
      std::exit(1);
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
  SoraClientConfig config_;
  std::shared_ptr<sora::SoraSignaling> conn_;
  std::unique_ptr<boost::asio::io_context> ioc_;
  std::unique_ptr<boost::asio::deadline_timer> timer_;
  std::set<std::string> opened_;
  bool ok_ = false;
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

  rtc::LogMessage::LogToDebug(rtc::LS_ERROR);
  rtc::LogMessage::LogTimestamps();
  rtc::LogMessage::LogThreads();

  boost::json::value v;
  {
    std::ifstream ifs(argv[1]);
    std::ostringstream oss;
    oss << ifs.rdbuf();
    std::string js = oss.str();
    v = boost::json::parse(js);
  }
  SoraClientConfig config;
  config.use_audio_deivce = false;
  config.use_hardware_encoder = false;
  for (auto&& x : v.as_object().at("signaling_urls").as_array()) {
    config.signaling_urls.push_back(x.as_string().c_str());
  }
  config.channel_id = v.as_object().at("channel_id").as_string().c_str();
  config.role = "sendrecv";

  for (int i = 0; i < 10; i++) {
    auto client = sora::CreateSoraClient<SoraClient>(config);
    client->Run();
  }
}
