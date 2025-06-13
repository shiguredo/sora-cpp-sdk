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
    context_config.video_codec_factory_config.capability_config.openh264_path =
        "...";
    auto& preference =
        context_config.video_codec_factory_config.preference.emplace();
    auto capability = sora::GetVideoCodecCapability(
        context_config.video_codec_factory_config.capability_config);
    preference.Merge(sora::CreateVideoCodecPreferenceFromImplementation(
        capability, sora::VideoCodecImplementation::kInternal));
    preference.Merge(sora::CreateVideoCodecPreferenceFromImplementation(
        capability, sora::VideoCodecImplementation::kCiscoOpenH264));
    context_ = sora::SoraClientContext::Create(context_config);

    ioc_.reset(new boost::asio::io_context(1));

    FakeVideoCapturerConfig fake_config;
    fake_config.width = 1920;
    fake_config.height = 1080;
    fake_config.fps = 30;
    auto video_source = CreateFakeVideoCapturer(fake_config);

    sora::SoraSignalingWhipConfig config;
    config.pc_factory = context_->peer_connection_factory();
    config.io_context = ioc_.get();
    config.signaling_url = "...";
    config.channel_id = "sora";
    config.video_source = video_source;

    auto& send_encodings = config.send_encodings.emplace();
    webrtc::RtpCodecCapability vp9_codec;
    webrtc::RtpCodecCapability av1_codec;
    webrtc::RtpCodecCapability h264_codec;
    webrtc::RtpCodecCapability h265_codec;
    vp9_codec.kind = webrtc::MediaType::VIDEO;
    vp9_codec.name = "VP9";
    vp9_codec.parameters["profile-id"] = "0";
    vp9_codec.clock_rate = 90000;
    av1_codec.kind = webrtc::MediaType::VIDEO;
    av1_codec.name = "AV1";
    av1_codec.clock_rate = 90000;
    av1_codec.parameters["level-idx"] = "5";
    av1_codec.parameters["profile"] = "0";
    av1_codec.parameters["tier"] = "0";
    h264_codec.kind = webrtc::MediaType::VIDEO;
    h264_codec.name = "H264";
    h264_codec.clock_rate = 90000;
    //h264_codec.parameters["profile-level-id"] = "42001f";
    //h264_codec.parameters["level-asymmetry-allowed"] = "1";
    //h264_codec.parameters["packetization-mode"] = "1";
    h265_codec.kind = webrtc::MediaType::VIDEO;
    h265_codec.name = "H265";
    h265_codec.clock_rate = 90000;
    send_encodings.resize(3);
    send_encodings[0].rid = "r0";
    send_encodings[0].scale_resolution_down_by = 4.0;
    send_encodings[1].rid = "r1";
    send_encodings[1].scale_resolution_down_by = 2.0;
    send_encodings[2].rid = "r2";
    send_encodings[2].scale_resolution_down_by = 1.0;

    send_encodings[0].codec = av1_codec;
    send_encodings[0].scalability_mode = "L1T2";
    //send_encodings[0].codec = h264_codec;

    send_encodings[1].codec = av1_codec;
    send_encodings[1].scalability_mode = "L1T2";
    //send_encodings[1].codec = h264_codec;

    //send_encodings[2].codec = av1_codec;
    //send_encodings[2].scalability_mode = "L1T2";
    //send_encodings[2].codec = h264_codec;
    //send_encodings[2].codec = h265_codec;
    send_encodings[2].codec = vp9_codec;

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
  if (!com_initializer.Succeeded()) {
    std::cerr << "CoInitializeEx failed" << std::endl;
    return 1;
  }
#endif

  rtc::LogMessage::LogToDebug(rtc::LS_INFO);
  rtc::LogMessage::LogTimestamps();
  rtc::LogMessage::LogThreads();

  auto client = std::make_shared<SoraClient>();
  client->Run();
}
