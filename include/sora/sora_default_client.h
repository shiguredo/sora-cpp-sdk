#ifndef SORA_SORA_DEFAULT_CLIENT_H_
#define SORA_SORA_DEFAULT_CLIENT_H_

// WebRTC
#include <api/peer_connection_interface.h>
#include <pc/connection_context.h>

#include "sora/sora_signaling.h"

namespace sora {

struct SoraDefaultClientConfig {
  // オーディオデバイスを利用するかどうか
  // false にすると一切オーディオデバイスを掴まなくなる
  bool use_audio_deivce = true;
  // ハードウェアエンコーダ/デコーダを利用するかどうか
  // false にするとソフトウェアエンコーダ/デコーダのみになる（H.264 は利用できない）
  bool use_hardware_encoder = true;
};

// Sora クライアントのデフォルトの実装
// 必要なスレッドの実行や、PeerConnectionFactory の生成を行う。
//
// 使い方:
//   class MyClient : public SoraDefaultClient {
//    public:
//     MyClient() : SoraDefaultClient(SoraDefaultClientConfig()) {}
//     // このあたりは必要ならオーバーライドする
//     void ConfigureDependencies(
//         webrtc::PeerConnectionFactoryDependencies& dependencies) override { ... }
//     void OnConfigured() override { ... }
//     void* GetAndroidApplicationContext(void* env) override { ... }
//
//     void Run();
//   };
//
//   std::shared_ptr<MyClient> client = sora::CreateSoraClient<MyClient>();
//   ...
//   client->Run();
//
// 独自の初期化をしたい場合はこのクラスを無理に利用する必要は無く、
// 必要に応じて自前で用意するのが良い。
class SoraDefaultClient : public sora::SoraSignalingObserver {
 public:
  SoraDefaultClient(SoraDefaultClientConfig config);
  bool Configure();

  // PeerConnectionFactoryDependencies をカスタマイズするためのコールバック関数
  // 値が設定された上で、PeerConnectionFactory を生成する直前に呼ばれる
  virtual void ConfigureDependencies(
      webrtc::PeerConnectionFactoryDependencies& dependencies) {}
  // PeerConnectionFactory を生成した後に呼ばれるコールバック関数
  virtual void OnConfigured() {}

  // Android の android.context.Context オブジェクトを返す関数
  // Android プラットフォームに対応する場合は Application#getApplicationContext()
  // で得られたオブジェクトを返す必要がある。
  // Android プラットフォームに対応しない場合はデフォルト実装のままで良い。
  virtual void* GetAndroidApplicationContext(void* env) { return nullptr; }

  // SoraSignalingObserver の実装
  void OnSetOffer() override {}
  void OnDisconnect(sora::SoraSignalingErrorCode ec,
                    std::string message) override {}
  void OnNotify(std::string text) override {}
  void OnPush(std::string text) override {}
  void OnMessage(std::string label, std::string data) override {}
  void OnTrack(rtc::scoped_refptr<webrtc::RtpTransceiverInterface> transceiver)
      override {}
  void OnRemoveTrack(
      rtc::scoped_refptr<webrtc::RtpReceiverInterface> receiver) override {}
  void OnDataChannel(std::string label) override {}

  rtc::Thread* network_thread() const { return network_thread_.get(); }
  rtc::Thread* worker_thread() const { return worker_thread_.get(); }
  rtc::Thread* signaling_thread() const { return signaling_thread_.get(); }
  rtc::scoped_refptr<webrtc::PeerConnectionFactoryInterface> factory() const {
    return factory_;
  }
  rtc::scoped_refptr<webrtc::ConnectionContext> connection_context() const {
    return connection_context_;
  }

 private:
  SoraDefaultClientConfig config_;
  std::unique_ptr<rtc::Thread> network_thread_;
  std::unique_ptr<rtc::Thread> worker_thread_;
  std::unique_ptr<rtc::Thread> signaling_thread_;
  rtc::scoped_refptr<webrtc::PeerConnectionFactoryInterface> factory_;
  rtc::scoped_refptr<webrtc::ConnectionContext> connection_context_;
};

// SoraDefaultClient を継承したクラスのオブジェクトを生成する関数
template <class T, class... Args>
static std::shared_ptr<T> CreateSoraClient(Args&&... args) {
  auto client = std::make_shared<T>(std::forward<Args>(args)...);
  if (!client->Configure()) {
    return nullptr;
  }
  return client;
}

}  // namespace sora
#endif