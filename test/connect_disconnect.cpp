// 接続と切断を繰り返すテスト
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
#include "sora/sora_client_context.h"
#include "sora/sora_video_decoder_factory.h"
#include "sora/sora_video_encoder_factory.h"

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
    video_track_ = nullptr;
    audio_track_ = nullptr;
    video_source_ = nullptr;
  }

  void Run() {
    sora::CameraDeviceCapturerConfig cam_config;
    cam_config.width = 640;
    cam_config.height = 480;
    cam_config.fps = 30;
    video_source_ = sora::CreateCameraDeviceCapturer(cam_config);

    std::string audio_track_id = rtc::CreateRandomString(16);
    std::string video_track_id = rtc::CreateRandomString(16);
    audio_track_ = pc_factory()->CreateAudioTrack(
        audio_track_id,
        pc_factory()->CreateAudioSource(cricket::AudioOptions()).get());
    video_track_ =
        pc_factory()->CreateVideoTrack(video_track_id, video_source_.get());

    ioc_.reset(new boost::asio::io_context(1));

    sora::SoraSignalingConfig config;
    config.pc_factory = pc_factory();
    config.io_context = ioc_.get();
    config.observer = shared_from_this();
    config.signaling_urls = config_.signaling_urls;
    config.channel_id = config_.channel_id;
    config.sora_client = "Hello Sora";
    config.role = config_.role;
    config.video_codec_type = "H264";
    config.multistream = true;
    conn_ = sora::SoraSignaling::Create(config);

    boost::asio::executor_work_guard<boost::asio::io_context::executor_type>
        work_guard(ioc_->get_executor());

    boost::asio::signal_set signals(*ioc_, SIGINT, SIGTERM);
    signals.async_wait(
        [this](const boost::system::error_code&, int) { conn_->Disconnect(); });

    timer_.reset(new boost::asio::deadline_timer(*ioc_));
    timer_->expires_from_now(boost::posix_time::seconds(1));
    timer_->async_wait([this](boost::system::error_code ec) {
      if (ec) {
        return;
      }
      conn_->Disconnect();
    });

    conn_->Connect();
    ioc_->run();
  }

  void OnSetOffer(std::string offer) override {
    std::string stream_id = rtc::CreateRandomString(16);
    webrtc::RTCErrorOr<rtc::scoped_refptr<webrtc::RtpSenderInterface>>
        audio_result =
            conn_->GetPeerConnection()->AddTrack(audio_track_, {stream_id});
    webrtc::RTCErrorOr<rtc::scoped_refptr<webrtc::RtpSenderInterface>>
        video_result =
            conn_->GetPeerConnection()->AddTrack(video_track_, {stream_id});
  }
  void OnDisconnect(sora::SoraSignalingErrorCode ec,
                    std::string message) override {
    RTC_LOG(LS_INFO) << "OnDisconnect: " << message;
    ioc_->stop();
  }
  void OnNotify(std::string text) override {}
  void OnPush(std::string text) override {}
  void OnMessage(std::string label, std::string data) override {}

  void OnTrack(rtc::scoped_refptr<webrtc::RtpTransceiverInterface> transceiver)
      override {}
  void OnRemoveTrack(
      rtc::scoped_refptr<webrtc::RtpReceiverInterface> receiver) override {}

  void OnDataChannel(std::string label) override {}

 private:
  rtc::scoped_refptr<webrtc::PeerConnectionFactoryInterface> pc_factory() {
    return context_->peer_connection_factory();
  }

 private:
  std::shared_ptr<sora::SoraClientContext> context_;
  SoraClientConfig config_;
  rtc::scoped_refptr<webrtc::VideoTrackSourceInterface> video_source_;
  rtc::scoped_refptr<webrtc::AudioTrackInterface> audio_track_;
  rtc::scoped_refptr<webrtc::VideoTrackInterface> video_track_;
  std::shared_ptr<sora::SoraSignaling> conn_;
  std::unique_ptr<boost::asio::io_context> ioc_;
  std::unique_ptr<boost::asio::deadline_timer> timer_;
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

  //rtc::LogMessage::LogToDebug(rtc::LS_VERBOSE);
  //rtc::LogMessage::LogTimestamps();
  //rtc::LogMessage::LogThreads();

  auto context =
      sora::SoraClientContext::Create(sora::SoraClientContextConfig());

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
