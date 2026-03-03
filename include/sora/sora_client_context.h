#ifndef SORA_SORA_CLIENT_CONTEXT_H_
#define SORA_SORA_CLIENT_CONTEXT_H_

#include <functional>
#include <memory>
#include <optional>
#include <string>

// WebRTC
#include <api/peer_connection_interface.h>
#include <api/scoped_refptr.h>
#include <pc/connection_context.h>
#include <rtc_base/thread.h>

#include "sora/sora_video_codec_factory.h"

namespace sora {

struct SoraClientContextConfig {
  // オーディオデバイスを利用するかどうか
  // false にすると一切オーディオデバイスを掴まなくなる
  bool use_audio_device = true;
  // 録音デバイス名
  std::optional<std::string> audio_recording_device;
  // 再生デバイス名
  std::optional<std::string> audio_playout_device;
  // VideoEncoderFactory/VideoDecoderFactory に関する設定
  SoraVideoCodecFactoryConfig video_codec_factory_config;

  // PeerConnectionFactoryDependencies をカスタマイズするためのコールバック関数
  // デフォルトの値が設定された上で、PeerConnectionFactory を生成する直前に呼ばれる
  std::function<void(webrtc::PeerConnectionFactoryDependencies&)>
      configure_dependencies;

  // Android の android.context.Context オブジェクトを返す関数
  // Android プラットフォームに対応する場合は Application#getApplicationContext()
  // で得られたオブジェクトを返す必要がある。
  // Android プラットフォームに対応しない場合は未設定でよい。
  std::function<void*(void*)> get_android_application_context;
};

// Sora 向けクライアントを生成するためのデータを保持するクラス
//
// 必要なスレッドの実行や、PeerConnectionFactory の生成を行う。
//
// 使い方：
//   sora::SoraClientContextConfig context_config;
//   // 必要なら context_config をカスタマイズする
//   context_config.configure_dependencies = [](webrtc::PeerConnectionFactoryDependencies& dep) { ... };
//   // Android に対応する場合は get_android_application_context を設定する
//   context_config.get_android_application_context = [](void* env) { ... };
//
//   auto context = sora::SoraClientContext::Create(context_config);
//
//   // context を使って Sora のクライアントを生成する
//   auto client = std::make_shared<MyClient>(context);
class SoraClientContext {
 public:
  static std::shared_ptr<SoraClientContext> Create(
      const SoraClientContextConfig& config);

  ~SoraClientContext();

  webrtc::Thread* network_thread() const { return network_thread_.get(); }
  webrtc::Thread* worker_thread() const { return worker_thread_.get(); }
  webrtc::Thread* signaling_thread() const { return signaling_thread_.get(); }
  webrtc::scoped_refptr<webrtc::PeerConnectionFactoryInterface>
  peer_connection_factory() const {
    return factory_;
  }
  webrtc::scoped_refptr<webrtc::ConnectionContext> connection_context() const {
    return connection_context_;
  }
  const SoraClientContextConfig& config() const { return config_; }
  void* android_application_context(void* env) const {
    return config_.get_android_application_context
               ? config_.get_android_application_context(env)
               : nullptr;
  }

 private:
  SoraClientContextConfig config_;
  std::unique_ptr<webrtc::Thread> network_thread_;
  std::unique_ptr<webrtc::Thread> worker_thread_;
  std::unique_ptr<webrtc::Thread> signaling_thread_;
  webrtc::scoped_refptr<webrtc::PeerConnectionFactoryInterface> factory_;
  webrtc::scoped_refptr<webrtc::ConnectionContext> connection_context_;
};

}  // namespace sora

#endif