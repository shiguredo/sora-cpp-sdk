// SoraSignalingConfig::audio_adaptive_ptime が audio sender の
// RtpParameters::encodings[].adaptive_ptime に反映されることを実 Sora で確認する
//
// adaptivePtime は音声専用のパラメータで、libwebrtc では音声の voice engine だけが
// RtpEncodingParameters::adaptive_ptime を参照する。そのため映像側には設定されない
// ことも合わせて確認する。
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <memory>
#include <optional>
#include <string>

// Boost
#include <boost/asio/executor_work_guard.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/signal_set.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/system/detail/error_code.hpp>

// WebRTC
#include <api/audio_options.h>
#include <api/media_stream_interface.h>
#include <api/peer_connection_interface.h>
#include <api/rtp_parameters.h>
#include <api/rtp_receiver_interface.h>
#include <api/rtp_sender_interface.h>
#include <api/rtp_transceiver_interface.h>
#include <api/scoped_refptr.h>
#include <rtc_base/crypto_random.h>
#include <rtc_base/logging.h>

#ifdef _WIN32
#include <rtc_base/win/scoped_com_initializer.h>
#endif

// Catch2
#include <catch2/catch_test_macros.hpp>

// Sora C++ SDK
#include <sora/boost_json_iwyu.h>
#include <sora/capturer/fake_video_capturer.h>
#include <sora/sora_client_context.h>
#include <sora/sora_signaling.h>

namespace {

// 接続時に確認した sender の adaptive_ptime
struct AdaptivePtimeResult {
  // audio sender の encodings[0].adaptive_ptime
  // audio sender または encoding が取得できなかった場合は nullopt
  std::optional<bool> audio;
  // video sender の encodings[0].adaptive_ptime
  // video sender または encoding が取得できなかった場合は nullopt
  std::optional<bool> video;
};

// mid が一致する transceiver の sender を返す
// mid は Sora の offer メッセージで通知された値で、SoraSignaling が
// audio sender / video sender を特定する際に利用している値でもある
webrtc::scoped_refptr<webrtc::RtpSenderInterface> FindSender(
    const webrtc::scoped_refptr<webrtc::PeerConnectionInterface>& pc,
    const std::string& mid) {
  for (auto transceiver : pc->GetTransceivers()) {
    if (transceiver->mid() && *transceiver->mid() == mid) {
      return transceiver->sender();
    }
  }
  return nullptr;
}

// 先頭の encoding の adaptive_ptime を返す
// sender または encoding が取得できなかった場合は nullopt を返す
std::optional<bool> GetAdaptivePtime(
    const webrtc::scoped_refptr<webrtc::RtpSenderInterface>& sender) {
  if (sender == nullptr) {
    return std::nullopt;
  }
  auto parameters = sender->GetParameters();
  if (parameters.encodings.empty()) {
    return std::nullopt;
  }
  return parameters.encodings[0].adaptive_ptime;
}

// 実際の Sora へ接続して audio sender と video sender の adaptive_ptime を確認する
class SoraClient : public std::enable_shared_from_this<SoraClient>,
                   public sora::SoraSignalingObserver {
 public:
  explicit SoraClient(std::optional<bool> audio_adaptive_ptime)
      : audio_adaptive_ptime_(audio_adaptive_ptime) {}
  ~SoraClient() {
    RTC_LOG(LS_INFO) << "SoraClient dtor";
    timer_.reset();
    ioc_.reset();
    video_track_ = nullptr;
    video_source_ = nullptr;
    audio_track_ = nullptr;
    conn_.reset();
    context_.reset();
  }

  void Run() {
    sora::SoraClientContextConfig context_config;
    // このテストでは音声を送信できればよいので、実際のオーディオデバイスは利用しない
    context_config.use_audio_device = false;
    auto context = sora::SoraClientContext::Create(context_config);
    REQUIRE(context != nullptr);
    auto pc_factory = context->peer_connection_factory();
    context_ = context;

    ioc_.reset(new boost::asio::io_context(1));

    // 音声トラックと映像トラックを生成する
    std::string audio_track_id = webrtc::CreateRandomString(16);
    audio_track_ = pc_factory->CreateAudioTrack(
        audio_track_id,
        pc_factory->CreateAudioSource(webrtc::AudioOptions()).get());

    sora::FakeVideoCapturerConfig fake_config;
    fake_config.width = 640;
    fake_config.height = 480;
    fake_config.fps = 30;
    video_source_ = sora::FakeVideoCapturer::Create(fake_config);
    std::string video_track_id = webrtc::CreateRandomString(16);
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
    auto channel_id = std::string(run_number) + "-" + matrix_name +
                      "-sora-cpp-sdk-audio-adaptive-ptime-test";
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
    config.audio = true;
    config.multistream = true;
    config.audio_adaptive_ptime = audio_adaptive_ptime_;
    conn_ = sora::SoraSignaling::Create(config);

    boost::asio::executor_work_guard<boost::asio::io_context::executor_type>
        work_guard(ioc_->get_executor());

    boost::asio::signal_set signals(*ioc_, SIGINT, SIGTERM);
    signals.async_wait(
        [this](const boost::system::error_code&, int) { conn_->Disconnect(); });

    // 接続が確立できないままテストが終わらないようにするためのタイマー
    timer_.reset(new boost::asio::steady_timer(*ioc_));
    timer_->expires_after(std::chrono::seconds(10));
    timer_->async_wait([this](boost::system::error_code ec) {
      if (ec) {
        return;
      }
      conn_->Disconnect();
    });

    conn_->Connect();
    ioc_->run();
  }

  // 接続時に確認した adaptive_ptime を返す
  const AdaptivePtimeResult& result() const { return result_; }

  void OnSetOffer(std::string offer) override {
    // Sora の offer に対応する音声トラックと映像トラックを追加する
    std::string stream_id = webrtc::CreateRandomString(16);
    auto audio_result =
        conn_->GetPeerConnection()->AddTrack(audio_track_, {stream_id});
    REQUIRE(audio_result.ok());
    auto video_result =
        conn_->GetPeerConnection()->AddTrack(video_track_, {stream_id});
    REQUIRE(video_result.ok());
  }
  void OnDisconnect(sora::SoraSignalingErrorCode ec,
                    std::string message) override {
    RTC_LOG(LS_INFO) << "OnDisconnect: " << message;
    ioc_->stop();
  }
  void OnNotify(std::string text) override {
    auto v = boost::json::parse(text);
    if (v.at("type") == "notify" &&
        v.at("event_type") == "connection.created") {
      // 接続が確立した時点で adaptive_ptime の設定は完了しているため、
      // ここで audio sender と video sender のパラメータを確認する
      auto pc = conn_->GetPeerConnection();
      result_.audio = GetAdaptivePtime(FindSender(pc, conn_->GetAudioMid()));
      result_.video = GetAdaptivePtime(FindSender(pc, conn_->GetVideoMid()));
      conn_->Disconnect();
    }
  }
  void OnPush(std::string text) override {}
  void OnMessage(std::string label, std::string data) override {}
  void OnSwitched(std::string text) override {}

  void OnTrack(webrtc::scoped_refptr<webrtc::RtpTransceiverInterface>
                   transceiver) override {}
  void OnRemoveTrack(
      webrtc::scoped_refptr<webrtc::RtpReceiverInterface> receiver) override {}

  void OnDataChannel(std::string label) override {}

 private:
  std::optional<bool> audio_adaptive_ptime_;
  AdaptivePtimeResult result_;

  std::shared_ptr<sora::SoraClientContext> context_;
  std::shared_ptr<sora::SoraSignaling> conn_;
  std::unique_ptr<boost::asio::io_context> ioc_;
  std::unique_ptr<boost::asio::steady_timer> timer_;
  webrtc::scoped_refptr<webrtc::AudioTrackInterface> audio_track_;
  webrtc::scoped_refptr<webrtc::VideoTrackSourceInterface> video_source_;
  webrtc::scoped_refptr<webrtc::VideoTrackInterface> video_track_;
};

// 指定した audio_adaptive_ptime で Sora へ接続し、接続時に確認した
// adaptive_ptime を返す
AdaptivePtimeResult CheckAdaptivePtime(
    std::optional<bool> audio_adaptive_ptime) {
#ifdef _WIN32
  webrtc::ScopedCOMInitializer com_initializer(
      webrtc::ScopedCOMInitializer::kMTA);
  REQUIRE(com_initializer.Succeeded());
#endif

  webrtc::LogMessage::LogToDebug(webrtc::LS_ERROR);
  webrtc::LogMessage::LogTimestamps();
  webrtc::LogMessage::LogThreads();

  auto client = std::make_shared<SoraClient>(audio_adaptive_ptime);
  client->Run();
  return client->result();
}

}  // namespace

TEST_CASE(
    "audio_adaptive_ptime を true にすると audio sender の adaptive_ptime "
    "が true になること") {
  auto result = CheckAdaptivePtime(true);

  // Sora から通知された mid で audio sender と video sender を特定できること
  REQUIRE(result.audio.has_value());
  REQUIRE(result.video.has_value());
  // audio sender の adaptive_ptime が true になること
  REQUIRE(*result.audio);
  // audio_adaptive_ptime の設定は audio sender にのみ適用するため、video
  // sender の adaptive_ptime は変化しないこと
  REQUIRE_FALSE(*result.video);
}

TEST_CASE(
    "audio_adaptive_ptime を指定しない場合は audio sender の adaptive_ptime "
    "が既定値のままであること") {
  auto result = CheckAdaptivePtime(std::nullopt);

  // Sora から通知された mid で audio sender と video sender を特定できること
  REQUIRE(result.audio.has_value());
  REQUIRE(result.video.has_value());
  // audio_adaptive_ptime を指定していないため、audio sender の adaptive_ptime は
  // 既定値の false のままであること
  REQUIRE_FALSE(*result.audio);
  // audio_adaptive_ptime の設定は video sender に波及しないこと
  REQUIRE_FALSE(*result.video);
}

TEST_CASE(
    "audio_adaptive_ptime に false を指定した場合は audio sender の "
    "adaptive_ptime が false のままであること") {
  auto result = CheckAdaptivePtime(false);

  // Sora から通知された mid で audio sender と video sender を特定できること
  REQUIRE(result.audio.has_value());
  REQUIRE(result.video.has_value());
  // false を指定した場合も audio sender の adaptive_ptime は false のままで
  // あること
  REQUIRE_FALSE(*result.audio);
  // audio_adaptive_ptime の設定は video sender に波及しないこと
  REQUIRE_FALSE(*result.video);
}
