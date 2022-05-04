#include "hello.h"

#include <fstream>
#include <iostream>

// WebRTC
#include <api/audio_codecs/builtin_audio_decoder_factory.h>
#include <api/audio_codecs/builtin_audio_encoder_factory.h>
#include <api/create_peerconnection_factory.h>
#include <api/rtc_event_log/rtc_event_log_factory.h>
#include <api/task_queue/default_task_queue_factory.h>
#include <api/video_codecs/builtin_video_decoder_factory.h>
#include <api/video_codecs/builtin_video_encoder_factory.h>
#include <media/engine/webrtc_media_engine.h>
#include <modules/audio_device/include/audio_device.h>
#include <modules/audio_device/include/audio_device_factory.h>
#include <modules/audio_processing/include/audio_processing.h>
#include <modules/video_capture/video_capture.h>
#include <modules/video_capture/video_capture_factory.h>
#include <pc/video_track_source_proxy.h>
#include <rtc_base/logging.h>
#include <rtc_base/ssl_adapter.h>

#ifdef _WIN32
#include <rtc_base/win/scoped_com_initializer.h>
#endif

#include "sora/device_video_capturer.h"
#include "sora/sora_video_decoder_factory.h"
#include "sora/sora_video_encoder_factory.h"

#if defined(__APPLE__)
#include "sora/mac/mac_capturer.h"
#endif

HelloSora::HelloSora(HelloSoraConfig config) : config_(config) {}
void HelloSora::Init() {
  rtc::InitializeSSL();

  network_thread_ = rtc::Thread::CreateWithSocketServer();
  network_thread_->Start();
  worker_thread_ = rtc::Thread::Create();
  worker_thread_->Start();
  signaling_thread_ = rtc::Thread::Create();
  signaling_thread_->Start();

  webrtc::PeerConnectionFactoryDependencies dependencies;
  dependencies.network_thread = network_thread_.get();
  dependencies.worker_thread = worker_thread_.get();
  dependencies.signaling_thread = signaling_thread_.get();
  dependencies.task_queue_factory = webrtc::CreateDefaultTaskQueueFactory();
  dependencies.call_factory = webrtc::CreateCallFactory();
  dependencies.event_log_factory =
      absl::make_unique<webrtc::RtcEventLogFactory>(
          dependencies.task_queue_factory.get());

  // media_dependencies
  cricket::MediaEngineDependencies media_dependencies;
  media_dependencies.task_queue_factory = dependencies.task_queue_factory.get();
#if defined(_WIN32)
  media_dependencies.adm =
      worker_thread_->Invoke<rtc::scoped_refptr<webrtc::AudioDeviceModule>>(
          RTC_FROM_HERE, [&] {
            return webrtc::CreateWindowsCoreAudioAudioDeviceModule(
                dependencies.task_queue_factory.get());
          });
#else
  media_dependencies.adm =
      worker_thread_->Invoke<rtc::scoped_refptr<webrtc::AudioDeviceModule>>(
          RTC_FROM_HERE, [&] {
            return webrtc::AudioDeviceModule::Create(
                webrtc::AudioDeviceModule::kPlatformDefaultAudio,
                dependencies.task_queue_factory.get());
          });
#endif
  media_dependencies.audio_encoder_factory =
      webrtc::CreateBuiltinAudioEncoderFactory();
  media_dependencies.audio_decoder_factory =
      webrtc::CreateBuiltinAudioDecoderFactory();

  auto cuda_context = sora::CudaContext::Create();
  {
    auto config = sora::GetDefaultVideoEncoderFactoryConfig(cuda_context);
    config.use_simulcast_adapter = true;
    media_dependencies.video_encoder_factory =
        absl::make_unique<sora::SoraVideoEncoderFactory>(std::move(config));
  }
  {
    auto config = sora::GetDefaultVideoDecoderFactoryConfig(cuda_context);
    media_dependencies.video_decoder_factory =
        absl::make_unique<sora::SoraVideoDecoderFactory>(std::move(config));
  }

  media_dependencies.audio_mixer = nullptr;
  media_dependencies.audio_processing =
      webrtc::AudioProcessingBuilder().Create();

  dependencies.media_engine =
      cricket::CreateMediaEngine(std::move(media_dependencies));

  factory_ =
      webrtc::CreateModularPeerConnectionFactory(std::move(dependencies));

  webrtc::PeerConnectionFactoryInterface::Options factory_options;
  factory_options.disable_encryption = false;
  factory_options.ssl_max_version = rtc::SSL_PROTOCOL_DTLS_12;
  factory_options.crypto_options.srtp.enable_gcm_crypto_suites = true;
  factory_->SetOptions(factory_options);

#if defined(__APPLE__)
  auto video_track_source = sora::MacCapturer::Create(640, 480, 30, "");
#else
  auto video_track_source = sora::DeviceVideoCapturer::Create(640, 480, 30);
#endif

  std::string audio_track_id = "0123456789abcdef";
  std::string video_track_id = "0123456789abcdefg";
  audio_track_ = factory_->CreateAudioTrack(
      audio_track_id, factory_->CreateAudioSource(cricket::AudioOptions()));
  rtc::scoped_refptr<webrtc::VideoTrackSourceInterface> video_source =
      webrtc::VideoTrackSourceProxy::Create(
          signaling_thread_.get(), worker_thread_.get(), video_track_source);
  video_track_ = factory_->CreateVideoTrack(video_track_id, video_source);

  ioc_.reset(new boost::asio::io_context(1));

  sora::SoraSignalingConfig config;
  config.pc_factory = factory_;
  config.io_context = ioc_.get();
  config.observer = shared_from_this();
  config.signaling_urls = config_.signaling_urls;
  config.channel_id = config_.channel_id;
  config.sora_client = "Hello Sora";
  config.role = config_.role;
  config.video_codec_type = "H264";
  conn_ = sora::SoraSignaling::Create(config);
  if (conn_ == nullptr) {
    RTC_LOG(LS_ERROR) << "Failed to sora::SoraSignaling::Create";
    std::exit(2);
  }
}

void HelloSora::Run() {
  boost::asio::executor_work_guard<boost::asio::io_context::executor_type>
      work_guard(ioc_->get_executor());

  boost::asio::signal_set signals(*ioc_, SIGINT, SIGTERM);
  signals.async_wait(
      [this](const boost::system::error_code&, int) { conn_->Disconnect(); });

  conn_->Connect();
  ioc_->run();
}

void HelloSora::OnSetOffer() {
  std::string stream_id = "0123456789abcdef";
  webrtc::RTCErrorOr<rtc::scoped_refptr<webrtc::RtpSenderInterface>>
      audio_result =
          conn_->GetPeerConnection()->AddTrack(audio_track_, {stream_id});
  webrtc::RTCErrorOr<rtc::scoped_refptr<webrtc::RtpSenderInterface>>
      video_result =
          conn_->GetPeerConnection()->AddTrack(video_track_, {stream_id});
}
void HelloSora::OnDisconnect(sora::SoraSignalingErrorCode ec,
                             std::string message) {
  std::cout << "OnDisconnect: " << message << std::endl;
  ioc_->stop();
}

#if !defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE

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

  std::shared_ptr<HelloSora> hello(new HelloSora(config));
  hello->Init();
  hello->Run();
}

#endif
