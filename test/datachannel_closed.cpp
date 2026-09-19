// DataChannel が閉じられた際に OnDisconnect が呼ばれることを確認するテスト
//
// クライアント起点の Disconnect() を呼ばずに signaling DataChannel へ disconnect を送ると、
// Sora は DataChannel を閉じるが close メッセージは送らない。
// このとき SoraSignaling が DataChannel の close を検知し、
// SoraSignalingErrorCode::DATACHANNEL_CLOSED で OnDisconnect を 1 回だけ呼ぶことを確認する。
//
// このテストでは、クライアントから signaling DataChannel へ disconnect を送ることで、
// クライアントが切断処理を開始していない状態のまま Sora に DataChannel を閉じさせる。
// これにより、DataChannel が一方的に閉じられた場合と同じ検知経路を通る。
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

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
#include <api/rtp_receiver_interface.h>
#include <api/rtp_transceiver_interface.h>
#include <api/scoped_refptr.h>
#include <rtc_base/crypto_random.h>
#include <rtc_base/logging.h>

#ifdef _WIN32
#include <rtc_base/win/scoped_com_initializer.h>
#endif

// Sora C++ SDK
#include <sora/boost_json_iwyu.h>
#include <sora/capturer/fake_video_capturer.h>
#include <sora/sora_client_context.h>
#include <sora/sora_signaling.h>

// OnDisconnect が呼ばれないまま終わらないようにするためのタイムアウト
constexpr int kConnectTimeoutSeconds = 20;
// OnDisconnect の二重通知を検出するために、1 回目の通知後に待つ時間
constexpr int kDisconnectWaitSeconds = 1;

struct SoraClientConfig {
  std::vector<std::string> signaling_urls;
  std::string channel_id;
};

class SoraClient : public std::enable_shared_from_this<SoraClient>,
                   public sora::SoraSignalingObserver {
 public:
  SoraClient(std::shared_ptr<sora::SoraClientContext> context,
             SoraClientConfig config)
      : context_(context), config_(config) {}
  ~SoraClient() {
    RTC_LOG(LS_INFO) << "SoraClient のデストラクタ";
    timer_.reset();
    ioc_.reset();
    video_track_ = nullptr;
    audio_track_ = nullptr;
    video_source_ = nullptr;
  }

  void Run() {
    // Sora は sora.conf の data_channel_messaging_only が有効でない限り音声と映像を
    // 両方 false にした接続を拒否するため、テスト用の映像と音声を用意する
    sora::FakeVideoCapturerConfig fake_config;
    fake_config.width = 640;
    fake_config.height = 480;
    fake_config.fps = 30;
    video_source_ = sora::FakeVideoCapturer::Create(fake_config);
    video_track_ = pc_factory()->CreateVideoTrack(
        video_source_, webrtc::CreateRandomString(16));
    audio_track_ = pc_factory()->CreateAudioTrack(
        webrtc::CreateRandomString(16),
        pc_factory()->CreateAudioSource(webrtc::AudioOptions()).get());

    ioc_.reset(new boost::asio::io_context(1));

    sora::SoraSignalingConfig config;
    config.pc_factory = pc_factory();
    config.io_context = ioc_.get();
    config.observer = shared_from_this();
    config.signaling_urls = config_.signaling_urls;
    config.channel_id = config_.channel_id;
    config.role = "sendonly";
    config.video = true;
    config.audio = true;
    // DataChannel の close を検知するために DataChannel シグナリングを有効にする
    config.data_channel_signaling = true;
    // WebSocket を閉じても接続を維持し、DataChannel だけで切断を検知する状況にする
    config.ignore_disconnect_websocket = true;
    conn_ = sora::SoraSignaling::Create(config);

    boost::asio::executor_work_guard<boost::asio::io_context::executor_type>
        work_guard(ioc_->get_executor());

    boost::asio::signal_set signals(*ioc_, SIGINT, SIGTERM);
    signals.async_wait(
        [this](const boost::system::error_code&, int) { conn_->Disconnect(); });

    timer_.reset(new boost::asio::steady_timer(*ioc_));
    timer_->expires_after(std::chrono::seconds(kConnectTimeoutSeconds));
    timer_->async_wait([this](boost::system::error_code ec) {
      if (ec) {
        return;
      }
      RTC_LOG(LS_ERROR) << "失敗: DataChannel が閉じられても OnDisconnect "
                           "が呼ばれませんでした";
      std::exit(1);
    });

    conn_->Connect();
    ioc_->run();

    // OnDisconnect が 1 回だけ DATACHANNEL_CLOSED で呼ばれたことを確認する
    if (!switched_received_) {
      RTC_LOG(LS_ERROR)
          << "失敗: DataChannel シグナリングへ切り替わりませんでした";
      std::exit(1);
    }
    if (disconnect_count_ != 1) {
      RTC_LOG(LS_ERROR)
          << "失敗: OnDisconnect の呼び出し回数が 1 回ではありません: count="
          << disconnect_count_;
      std::exit(1);
    }
    if (disconnect_ec_ != sora::SoraSignalingErrorCode::DATACHANNEL_CLOSED) {
      RTC_LOG(LS_ERROR) << "失敗: OnDisconnect のエラーコードが "
                           "DATACHANNEL_CLOSED ではありません: message="
                        << disconnect_message_;
      std::exit(1);
    }

    RTC_LOG(LS_INFO) << "成功: DataChannel の close を検知して OnDisconnect が "
                        "1 回呼ばれました";
  }

  void OnSetOffer(std::string offer) override {
    // offer の setRemoteDescription が終わった後にトラックを追加する
    std::string stream_id = webrtc::CreateRandomString(16);
    conn_->GetPeerConnection()->AddTrack(audio_track_, {stream_id});
    conn_->GetPeerConnection()->AddTrack(video_track_, {stream_id});
  }

  void OnDisconnect(sora::SoraSignalingErrorCode ec,
                    std::string message) override {
    ++disconnect_count_;
    disconnect_ec_ = ec;
    disconnect_message_ = message;
    RTC_LOG(LS_INFO) << "OnDisconnect が呼ばれました: " << message;

    if (disconnect_count_ > 1) {
      // 二重通知を検出したので即座にテストを終わらせる
      ioc_->stop();
      return;
    }

    // 二重通知が来ないことを確認するために、しばらく待ってからイベントループを止める
    timer_->expires_after(std::chrono::seconds(kDisconnectWaitSeconds));
    timer_->async_wait([this](boost::system::error_code ec) {
      if (ec) {
        return;
      }
      ioc_->stop();
    });
  }

  void OnNotify(std::string text) override {}
  void OnPush(std::string text) override {}
  void OnMessage(std::string label, std::string data) override {}
  void OnDataChannel(std::string label) override {}

  void OnTrack(webrtc::scoped_refptr<webrtc::RtpTransceiverInterface>
                   transceiver) override {}
  void OnRemoveTrack(
      webrtc::scoped_refptr<webrtc::RtpReceiverInterface> receiver) override {}

  void OnSwitched(std::string text) override {
    switched_received_ = true;
    // クライアント起点の Disconnect() を呼ばずに signaling DataChannel へ disconnect を送る。
    // Sora はこれを受けて DataChannel を閉じるが close メッセージは送らないため、
    // DataChannel が一方的に閉じられた場合と同じ状況になる
    RTC_LOG(LS_INFO) << "switched を受信したため signaling DataChannel へ "
                        "disconnect を送ります";
    conn_->SendDataChannel("signaling",
                           R"({"type":"disconnect","reason":"NO-ERROR"})");
  }

 private:
  webrtc::scoped_refptr<webrtc::PeerConnectionFactoryInterface> pc_factory() {
    return context_->peer_connection_factory();
  }

 private:
  std::shared_ptr<sora::SoraClientContext> context_;
  SoraClientConfig config_;
  webrtc::scoped_refptr<webrtc::VideoTrackSourceInterface> video_source_;
  webrtc::scoped_refptr<webrtc::AudioTrackInterface> audio_track_;
  webrtc::scoped_refptr<webrtc::VideoTrackInterface> video_track_;
  std::shared_ptr<sora::SoraSignaling> conn_;
  std::unique_ptr<boost::asio::io_context> ioc_;
  std::unique_ptr<boost::asio::steady_timer> timer_;
  bool switched_received_ = false;
  int disconnect_count_ = 0;
  sora::SoraSignalingErrorCode disconnect_ec_ =
      sora::SoraSignalingErrorCode::CLOSE_SUCCEEDED;
  std::string disconnect_message_;
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

  webrtc::LogMessage::LogToDebug(webrtc::LS_ERROR);
  webrtc::LogMessage::LogTimestamps();
  webrtc::LogMessage::LogThreads();

  sora::SoraClientContextConfig context_config;
  context_config.use_audio_device = false;
  auto context = sora::SoraClientContext::Create(context_config);

  // テスト用のパラメータファイルを読み込む
  // test/.testparam.json のようにコメント付きの JSON でも読めるようにしておく
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
  SoraClientConfig config;
  for (auto&& x : v.as_object().at("signaling_urls").as_array()) {
    config.signaling_urls.push_back(x.as_string().c_str());
  }
  config.channel_id = v.as_object().at("channel_id").as_string().c_str();

  auto client = std::make_shared<SoraClient>(context, config);
  client->Run();

  return 0;
}
