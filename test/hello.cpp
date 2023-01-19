#include "hello.h"

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
#include "sora/sora_video_decoder_factory.h"
#include "sora/sora_video_encoder_factory.h"

#if defined(HELLO_ANDROID)
void* GetAndroidApplicationContext(void* env);
#else
void* GetAndroidApplicationContext(void* env) {
  return nullptr;
}
#endif

HelloSora::HelloSora(HelloSoraConfig config)
    : sora::SoraDefaultClient(config), config_(config) {}

HelloSora::~HelloSora() {
  RTC_LOG(LS_INFO) << "HelloSora dtor";
  ioc_.reset();
  video_track_ = nullptr;
  audio_track_ = nullptr;
  video_source_ = nullptr;
}

void HelloSora::Run() {
  void* env = sora::GetJNIEnv();

  std::string audio_track_id = rtc::CreateRandomString(16);
  audio_track_ = factory()->CreateAudioTrack(
      audio_track_id,
      factory()->CreateAudioSource(cricket::AudioOptions()).get());

  if (config_.mode == HelloSoraConfig::Mode::Hello) {
    sora::CameraDeviceCapturerConfig cam_config;
    cam_config.width = 640;
    cam_config.height = 480;
    cam_config.fps = 30;
    cam_config.jni_env = env;
    cam_config.application_context = GetAndroidApplicationContext(env);
    video_source_ = sora::CreateCameraDeviceCapturer(cam_config);
    std::string video_track_id = rtc::CreateRandomString(16);
    video_track_ =
        factory()->CreateVideoTrack(video_track_id, video_source_.get());
  }

  ioc_.reset(new boost::asio::io_context(1));

  sora::SoraSignalingConfig config;
  config.pc_factory = factory();
  config.io_context = ioc_.get();
  config.observer = shared_from_this();
  config.signaling_urls = config_.signaling_urls;
  config.channel_id = config_.channel_id;
  config.sora_client = "Hello Sora";
  config.role = config_.role;
  config.video_codec_type = "H264";
  config.multistream = true;
  if (config_.mode == HelloSoraConfig::Mode::Lyra) {
    config.video = false;
    config.sora_client = "Hello Sora with Lyra";
    config.audio_codec_type = "LYRA";
    config.audio_codec_lyra_params = {{"version", "1.3.0"}};
  }
  conn_ = sora::SoraSignaling::Create(config);

  boost::asio::executor_work_guard<boost::asio::io_context::executor_type>
      work_guard(ioc_->get_executor());

  boost::asio::signal_set signals(*ioc_, SIGINT, SIGTERM);
  signals.async_wait(
      [this](const boost::system::error_code&, int) { conn_->Disconnect(); });

  conn_->Connect();
  ioc_->run();
}

void* HelloSora::GetAndroidApplicationContext(void* env) {
  return ::GetAndroidApplicationContext(env);
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
    v = boost::json::parse(js);
  }
  HelloSoraConfig config;
  for (auto&& x : v.as_object().at("signaling_urls").as_array()) {
    config.signaling_urls.push_back(x.as_string().c_str());
  }
  config.channel_id = v.as_object().at("channel_id").as_string().c_str();
  config.role = "sendonly";

  auto hello = sora::CreateSoraClient<HelloSora>(config);
  hello->Run();
}

#endif
