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
    ioc_.reset();
    audio_track_ = nullptr;
  }

  void Run() {
    void* env = sora::GetJNIEnv();
    std::string audio_track_id = rtc::CreateRandomString(16);
    audio_track_ = factory()->CreateAudioTrack(
        audio_track_id,
        factory()->CreateAudioSource(cricket::AudioOptions()).get());

    ioc_.reset(new boost::asio::io_context(1));

    sora::SoraSignalingConfig config;
    config.pc_factory = factory();
    config.io_context = ioc_.get();
    config.observer = shared_from_this();
    config.signaling_urls = config_.signaling_urls;
    config.channel_id = config_.channel_id;
    config.sora_client = "Testing SoraClient for Lyra";
    config.role = config_.role;
    config.video = false;
    config.audio_codec_type = "LYRA";
    config.audio_codec_lyra_params = {{"version", "1.3.0"}};
    config.multistream = true;
    conn_ = sora::SoraSignaling::Create(config);

    boost::asio::executor_work_guard<boost::asio::io_context::executor_type>
        work_guard(ioc_->get_executor());

    boost::asio::signal_set signals(*ioc_, SIGINT, SIGTERM);
    signals.async_wait(
        [this](const boost::system::error_code&, int) { conn_->Disconnect(); });

    conn_->Connect();
    ioc_->run();
  }

  void OnSetOffer(std::string offer) override {
    std::string stream_id = rtc::CreateRandomString(16);
    webrtc::RTCErrorOr<rtc::scoped_refptr<webrtc::RtpSenderInterface>>
        audio_result =
            conn_->GetPeerConnection()->AddTrack(audio_track_, {stream_id});
  }
  void OnDisconnect(sora::SoraSignalingErrorCode ec,
                    std::string message) override {
    RTC_LOG(LS_INFO) << "OnDisconnect: " << message;
    ioc_->stop();
  }

 private:
  SoraClientConfig config_;
  rtc::scoped_refptr<webrtc::AudioTrackInterface> audio_track_;
  std::shared_ptr<sora::SoraSignaling> conn_;
  std::unique_ptr<boost::asio::io_context> ioc_;
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

  rtc::LogMessage::LogToDebug(rtc::LS_WARNING);
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
  for (auto&& x : v.as_object().at("signaling_urls").as_array()) {
    config.signaling_urls.push_back(x.as_string().c_str());
  }
  config.channel_id = v.as_object().at("channel_id").as_string().c_str();
  config.role = "sendrecv";

  auto client = sora::CreateSoraClient<SoraClient>(config);
  client->Run();
}
