#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iostream>

// WebRTC
#include <rtc_base/logging.h>

#ifdef _WIN32
#include <rtc_base/win/scoped_com_initializer.h>
#endif

// Catch2
#include <catch2/catch_test_macros.hpp>

// Boost.Process
#include <boost/process/env.hpp>

// Sora
#include <sora/audio_device_module.h>
#include <sora/camera_device_capturer.h>
#include <sora/java_context.h>
#include <sora/sora_client_context.h>
#include <sora/sora_video_decoder_factory.h>
#include <sora/sora_video_encoder_factory.h>

#include "fake_video_capturer.h"

class SoraClient : public std::enable_shared_from_this<SoraClient>,
                   public sora::SoraSignalingObserver {
 public:
  SoraClient() {}
  ~SoraClient() {
    RTC_LOG(LS_INFO) << "SoraClient dtor";
    timer_.reset();
    ioc_.reset();
    video_track_ = nullptr;
    video_source_ = nullptr;
    conn_.reset();
    context_.reset();
  }

  void Run() {
    sora::SoraClientContextConfig context_config;
    context_config.use_audio_device = false;
    context_config.use_hardware_encoder = false;
    auto context = sora::SoraClientContext::Create(context_config);
    auto pc_factory = context->peer_connection_factory();
    context_ = context;

    ioc_.reset(new boost::asio::io_context(1));

    FakeVideoCapturerConfig fake_config;
    fake_config.width = 640;
    fake_config.height = 480;
    fake_config.fps = 30;
    video_source_ = CreateFakeVideoCapturer(fake_config);
    std::string video_track_id = rtc::CreateRandomString(16);
    video_track_ = pc_factory->CreateVideoTrack(video_source_, video_track_id);

    sora::SoraSignalingConfig config;
    auto signaling_url = std::getenv("TEST_SIGNALING_URL");
    auto channel_id_prefix = std::getenv("TEST_CHANNEL_ID_PREFIX");
    auto secret_key = std::getenv("TEST_SECRET_KEY");
    auto run_number = std::getenv("GITHUB_RUN_NUMBER") == nullptr
                          ? ""
                          : std::getenv("GITHUB_RUN_NUMBER");
    auto matrix_name = std::getenv("TEST_MATRIX_NAME") == nullptr
                           ? ""
                           : std::getenv("TEST_MATRIX_NAME");
    REQUIRE(signaling_url != nullptr);
    REQUIRE(channel_id_prefix != nullptr);
    config.signaling_urls.push_back(signaling_url);
    auto channel_id =
        std::string(run_number) + "-" + matrix_name + "-sora-cpp-sdk-e2e-test";
    RTC_LOG(LS_ERROR) << "channel_id="
                      << ("${TEST_CHANNEL_ID_PREFIX}" + channel_id);
    config.channel_id = channel_id_prefix + channel_id;
    if (secret_key != nullptr) {
      auto md = boost::json::object();
      md["access_token"] = std::string(secret_key);
      config.metadata = md;
    }

    config.pc_factory = pc_factory;
    config.io_context = ioc_.get();
    config.observer = shared_from_this();
    config.role = "sendonly";
    config.video = true;
    config.audio = false;
    config.multistream = true;
    conn_ = sora::SoraSignaling::Create(config);

    boost::asio::executor_work_guard<boost::asio::io_context::executor_type>
        work_guard(ioc_->get_executor());

    boost::asio::signal_set signals(*ioc_, SIGINT, SIGTERM);
    signals.async_wait(
        [this](const boost::system::error_code&, int) { conn_->Disconnect(); });

    timer_.reset(new boost::asio::deadline_timer(*ioc_));
    timer_->expires_from_now(boost::posix_time::seconds(5));
    timer_->async_wait([this](boost::system::error_code ec) {
      if (ec) {
        return;
      }
      ok_ = true;

      conn_->Disconnect();
    });

    conn_->Connect();
    ioc_->run();
    REQUIRE(ok_);
    REQUIRE_FALSE(connection_id_.empty());
  }

  void OnSetOffer(std::string offer) override {
    auto v = boost::json::parse(offer);
    if (v.at("type") == "offer") {
      connection_id_ = v.at("connection_id").as_string();
    }
  }
  void OnDisconnect(sora::SoraSignalingErrorCode ec,
                    std::string message) override {
    RTC_LOG(LS_INFO) << "OnDisconnect: " << message;
    REQUIRE(ok_);
    ioc_->stop();
  }
  void OnNotify(std::string text) override {
    auto v = boost::json::parse(text);
    if (v.at("type") == "notify" &&
        v.at("event_type") == "connection.created") {
      REQUIRE(connection_id_ == v.at("connection_id").as_string());
    }
  }
  void OnPush(std::string text) override {}
  void OnMessage(std::string label, std::string data) override {}
  void OnSwitched(std::string text) override {}

  void OnTrack(rtc::scoped_refptr<webrtc::RtpTransceiverInterface> transceiver)
      override {}
  void OnRemoveTrack(
      rtc::scoped_refptr<webrtc::RtpReceiverInterface> receiver) override {}

  void OnDataChannel(std::string label) override {}

 private:
  std::shared_ptr<sora::SoraClientContext> context_;
  std::shared_ptr<sora::SoraSignaling> conn_;
  std::unique_ptr<boost::asio::io_context> ioc_;
  std::unique_ptr<boost::asio::deadline_timer> timer_;
  rtc::scoped_refptr<webrtc::VideoTrackSourceInterface> video_source_;
  rtc::scoped_refptr<webrtc::VideoTrackInterface> video_track_;
  std::atomic<bool> ok_{false};
  std::string connection_id_;
};

TEST_CASE("Sora に接続して切断するだけ") {
#ifdef _WIN32
  webrtc::ScopedCOMInitializer com_initializer(
      webrtc::ScopedCOMInitializer::kMTA);
  REQUIRE(com_initializer.Succeeded());
#endif

  rtc::LogMessage::LogToDebug(rtc::LS_ERROR);
  rtc::LogMessage::LogTimestamps();
  rtc::LogMessage::LogThreads();

  auto client = std::make_shared<SoraClient>();
  client->Run();
}
