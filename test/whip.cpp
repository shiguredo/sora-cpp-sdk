#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iostream>

// WebRTC
#include <rtc_base/logging.h>

#ifdef _WIN32
#include <rtc_base/win/scoped_com_initializer.h>
#endif

// Sora
#include <sora/sora_client_context.h>
#include <sora/sora_signaling_whip.h>

#include "fake_video_capturer.h"

class SoraClient : public std::enable_shared_from_this<SoraClient>,
                   public sora::SoraSignalingWhipObserver {
 public:
  SoraClient() {}
  ~SoraClient() {
    RTC_LOG(LS_INFO) << "SoraClient dtor";
    ioc_.reset();
    conn_.reset();
  }

  void Run() {
    sora::SoraClientContextConfig context_config;
    context_config.use_audio_device = false;
    context_config.use_hardware_encoder = false;
    context_ = sora::SoraClientContext::Create(context_config);

    ioc_.reset(new boost::asio::io_context(1));

    FakeVideoCapturerConfig fake_config;
    fake_config.width = 640;
    fake_config.height = 480;
    fake_config.fps = 30;
    auto video_source = CreateFakeVideoCapturer(fake_config);

    sora::SoraSignalingWhipConfig config;
    config.pc_factory = context_->peer_connection_factory();
    config.io_context = ioc_.get();
    config.signaling_url = "hoge-";
    config.channel_id = "sora";
    config.video_source = video_source;
    conn_ = sora::SoraSignalingWhip::Create(config);

    boost::asio::executor_work_guard<boost::asio::io_context::executor_type>
        work_guard(ioc_->get_executor());

    //boost::asio::signal_set signals(*ioc_, SIGINT, SIGTERM);
    //signals.async_wait(
    //    [this](const boost::system::error_code&, int) { conn_->Disconnect(); });

    conn_->Connect();
    ioc_->run();
  }

 private:
  std::shared_ptr<sora::SoraSignalingWhip> conn_;
  std::unique_ptr<boost::asio::io_context> ioc_;
  std::shared_ptr<sora::SoraClientContext> context_;
};

int main() {
#ifdef _WIN32
  webrtc::ScopedCOMInitializer com_initializer(
      webrtc::ScopedCOMInitializer::kMTA);
  REQUIRE(com_initializer.Succeeded());
#endif

  rtc::LogMessage::LogToDebug(rtc::LS_INFO);
  rtc::LogMessage::LogTimestamps();
  rtc::LogMessage::LogThreads();

  auto client = std::make_shared<SoraClient>();
  client->Run();
}
