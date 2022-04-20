#include <fstream>
#include <iostream>

// WebRTC
#include <absl/memory/memory.h>
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

#include "device_video_capturer.h"
#include "sora_signaling.h"

struct HelloSoraConfig {
  std::vector<std::string> signaling_urls;
  std::string channel_id;
  std::string role;
};

class HelloSora : public std::enable_shared_from_this<HelloSora>,
                  public sora::SoraSignalingObserver {
 public:
  HelloSora(HelloSoraConfig config) : config_(config) {}
  void Init() {
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
    media_dependencies.task_queue_factory =
        dependencies.task_queue_factory.get();
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

    media_dependencies.video_encoder_factory =
        webrtc::CreateBuiltinVideoEncoderFactory();
    media_dependencies.video_decoder_factory =
        webrtc::CreateBuiltinVideoDecoderFactory();

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

    auto video_track_source = sora::DeviceVideoCapturer::Create(640, 480, 30);

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
    config.role = config_.role;
    conn_ = sora::SoraSignaling::Create(config);
    if (conn_ == nullptr) {
      RTC_LOG(LS_ERROR) << "Failed to sora::SoraSignaling::Create";
      std::exit(2);
    }
  }

  void Run() {
    boost::asio::executor_work_guard<boost::asio::io_context::executor_type>
        work_guard(ioc_->get_executor());

    boost::asio::signal_set signals(*ioc_, SIGINT, SIGTERM);
    signals.async_wait(
        [this](const boost::system::error_code&, int) { conn_->Disconnect(); });

    conn_->Connect();
    ioc_->run();
  }

  void OnSetOffer() override {
    std::string stream_id = "0123456789abcdef";
    webrtc::RTCErrorOr<rtc::scoped_refptr<webrtc::RtpSenderInterface>>
        audio_result =
            conn_->GetPeerConnection()->AddTrack(audio_track_, {stream_id});
    webrtc::RTCErrorOr<rtc::scoped_refptr<webrtc::RtpSenderInterface>>
        video_result =
            conn_->GetPeerConnection()->AddTrack(video_track_, {stream_id});
  }
  void OnDisconnect(sora::SoraSignalingErrorCode ec,
                    std::string message) override {
    ioc_->stop();
  }
  void OnNotify(std::string text) override {}
  void OnPush(std::string text) override {}
  void OnMessage(std::string label, std::string data) override {}

 private:
  HelloSoraConfig config_;
  std::unique_ptr<rtc::Thread> network_thread_;
  std::unique_ptr<rtc::Thread> worker_thread_;
  std::unique_ptr<rtc::Thread> signaling_thread_;
  rtc::scoped_refptr<webrtc::PeerConnectionFactoryInterface> factory_;
  rtc::scoped_refptr<webrtc::AudioTrackInterface> audio_track_;
  rtc::scoped_refptr<webrtc::VideoTrackInterface> video_track_;
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
  config.role = v.as_object().at("role").as_string().c_str();

  std::shared_ptr<HelloSora> hello(new HelloSora(config));
  hello->Init();
  hello->Run();
}