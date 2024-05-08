#include "hello.h"

#include <fstream>
#include <iostream>

// WebRTC
#include <rtc_base/logging.h>

#ifdef _WIN32
#include <rtc_base/win/scoped_com_initializer.h>
#endif

// Sora C++ SDK
#include <sora/audio_device_module.h>
#include <sora/camera_device_capturer.h>
#include <sora/java_context.h>
#include <sora/sora_video_decoder_factory.h>
#include <sora/sora_video_encoder_factory.h>

#include "fake_video_capturer.h"

#if defined(HELLO_ANDROID)
void* GetAndroidApplicationContext(void* env);
#else
void* GetAndroidApplicationContext(void* env) {
  return nullptr;
}
#endif

HelloSora::HelloSora(std::shared_ptr<sora::SoraClientContext> context,
                     HelloSoraConfig config)
    : context_(context), config_(config) {}

HelloSora::~HelloSora() {
  RTC_LOG(LS_INFO) << "HelloSora dtor";
  ioc_.reset();
  video_track_ = nullptr;
  audio_track_ = nullptr;
  video_source_ = nullptr;
}

void HelloSora::Run() {
  void* env = sora::GetJNIEnv();

  FakeVideoCapturerConfig fake_config;
  fake_config.width = config_.capture_width;
  fake_config.height = config_.capture_height;
  fake_config.fps = 30;
  video_source_ = CreateFakeVideoCapturer(fake_config);
  std::string video_track_id = rtc::CreateRandomString(16);
  video_track_ = pc_factory()->CreateVideoTrack(video_source_, video_track_id);

  std::string audio_track_id = rtc::CreateRandomString(16);
  audio_track_ = pc_factory()->CreateAudioTrack(
      audio_track_id,
      pc_factory()->CreateAudioSource(cricket::AudioOptions()).get());

  ioc_.reset(new boost::asio::io_context(1));

  sora::SoraSignalingConfig config;
  config.pc_factory = pc_factory();
  config.io_context = ioc_.get();
  config.observer = shared_from_this();
  config.signaling_urls = config_.signaling_urls;
  config.channel_id = config_.channel_id;
  config.role = config_.role;
  config.video = config_.video;
  config.audio = config_.audio;
  config.video_codec_type = config_.video_codec_type;
  config.video_bit_rate = config_.video_bit_rate;
  config.multistream = true;
  config.simulcast = config_.simulcast;
  conn_ = sora::SoraSignaling::Create(config);

  boost::asio::executor_work_guard<boost::asio::io_context::executor_type>
      work_guard(ioc_->get_executor());

  boost::asio::signal_set signals(*ioc_, SIGINT, SIGTERM);
  signals.async_wait(
      [this](const boost::system::error_code&, int) { conn_->Disconnect(); });

  conn_->Connect();
  ioc_->run();
}

void HelloSora::OnSetOffer(std::string offer) {
  std::string stream_id = rtc::CreateRandomString(16);
  webrtc::RTCErrorOr<rtc::scoped_refptr<webrtc::RtpSenderInterface>>
      audio_result =
          conn_->GetPeerConnection()->AddTrack(audio_track_, {stream_id});
  if (video_track_ != nullptr) {
    webrtc::RTCErrorOr<rtc::scoped_refptr<webrtc::RtpSenderInterface>>
        video_result =
            conn_->GetPeerConnection()->AddTrack(video_track_, {stream_id});
  }
}
void HelloSora::OnDisconnect(sora::SoraSignalingErrorCode ec,
                             std::string message) {
  RTC_LOG(LS_INFO) << "OnDisconnect: " << message;
  ioc_->stop();
}

#if defined(__ANDROID__) || (defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE)

// iOS, Android は main を使わない

#else

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
    boost::json::parse_options opt;
    opt.allow_comments = true;
    opt.allow_trailing_commas = true;
    v = boost::json::parse(js, {}, opt);
  }
  HelloSoraConfig config;
  for (auto&& x : v.as_object().at("signaling_urls").as_array()) {
    config.signaling_urls.push_back(x.as_string().c_str());
  }
  config.channel_id = v.as_object().at("channel_id").as_string().c_str();
  if (auto it = v.as_object().find("role"); it != v.as_object().end()) {
    config.role = it->value().as_string();
  }
  if (auto it = v.as_object().find("video"); it != v.as_object().end()) {
    config.video = it->value().as_bool();
  }
  if (auto it = v.as_object().find("audio"); it != v.as_object().end()) {
    config.audio = it->value().as_bool();
  }
  if (auto it = v.as_object().find("capture_width");
      it != v.as_object().end()) {
    config.capture_width = boost::json::value_to<int>(it->value());
  }
  if (auto it = v.as_object().find("capture_height");
      it != v.as_object().end()) {
    config.capture_height = boost::json::value_to<int>(it->value());
  }
  if (auto it = v.as_object().find("video_bit_rate");
      it != v.as_object().end()) {
    config.video_bit_rate = boost::json::value_to<int>(it->value());
  }
  if (auto it = v.as_object().find("video_codec_type");
      it != v.as_object().end()) {
    config.video_codec_type = it->value().as_string();
  }
  if (auto it = v.as_object().find("simulcast"); it != v.as_object().end()) {
    config.simulcast = it->value().as_bool();
  }

  sora::SoraClientContextConfig context_config;
  context_config.get_android_application_context = GetAndroidApplicationContext;
  if (auto it = v.as_object().find("use_hardware_encoder");
      it != v.as_object().end()) {
    context_config.use_hardware_encoder = it->value().as_bool();
  }
  if (auto it = v.as_object().find("openh264"); it != v.as_object().end()) {
    context_config.openh264 = it->value().as_string();
  }
  auto context = sora::SoraClientContext::Create(context_config);

  auto hello = std::make_shared<HelloSora>(context, config);
  hello->Run();
}

#endif
