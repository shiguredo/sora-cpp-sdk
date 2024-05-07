#ifndef SORA_SORA_CLIENT_CONTEXT_H_
#define SORA_SORA_CLIENT_CONTEXT_H_

// WebRTC
#include <api/environment/environment_factory.h>
#include <api/peer_connection_interface.h>
#include <media/engine/webrtc_media_engine.h>
#include <pc/connection_context.h>

#include "sora/sora_signaling.h"

namespace sora {

struct SoraClientContextConfig {
  // オーディオデバイスを利用するかどうか
  // false にすると一切オーディオデバイスを掴まなくなる
  bool use_audio_device = true;
  // ハードウェアエンコーダ/デコーダを利用するかどうか
  // false にするとソフトウェアエンコーダ/デコーダのみになる（H.264 は利用できない）
  bool use_hardware_encoder = true;
  // SoraVideoEncoderFactoryConfig に定義されている同名の変数をアプリケーションから設定するための変数
  bool force_i420_conversion_for_simulcast_adapter = true;

  // PeerConnectionFactoryDependencies をカスタマイズするためのコールバック関数
  // デフォルトの値が設定された上で、PeerConnectionFactory を生成する直前に呼ばれる
  std::function<void(webrtc::PeerConnectionFactoryDependencies&)>
      configure_dependencies;

  // Android の android.context.Context オブジェクトを返す関数
  // Android プラットフォームに対応する場合は Application#getApplicationContext()
  // で得られたオブジェクトを返す必要がある。
  // Android プラットフォームに対応しない場合は未設定でよい。
  std::function<void*(void*)> get_android_application_context;

  // OpenH264 の動的ライブラリのパス
  // 設定すると OpenH264 が H264 エンコーダの候補に含まれる。
  // ただし configure_dependencies をカスタマイズして dependencies.video_encoder_factory を
  // 上書きしている場合、OpenH264 が H264 エンコーダの候補に含まれない可能性があるので注意。
  std::optional<std::string> openh264;
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
      const SoraClientContextConfig& config,
      webrtc::Environment& env);

  static std::shared_ptr<SoraClientContext> Create(
      const SoraClientContextConfig& config);

  ~SoraClientContext();

  rtc::Thread* network_thread() const { return network_thread_.get(); }
  rtc::Thread* worker_thread() const { return worker_thread_.get(); }
  rtc::Thread* signaling_thread() const { return signaling_thread_.get(); }
  rtc::scoped_refptr<webrtc::PeerConnectionFactoryInterface>
  peer_connection_factory() const {
    return factory_;
  }
  rtc::scoped_refptr<webrtc::ConnectionContext> connection_context() const {
    return connection_context_;
  }
  const SoraClientContextConfig& config() const { return config_; }
  void* android_application_context(void* env) const {
    return config_.get_android_application_context
               ? config_.get_android_application_context(env)
               : nullptr;
  }
  std::shared_ptr<webrtc::Environment> env;

 private:
  SoraClientContextConfig config_;
  std::unique_ptr<rtc::Thread> network_thread_;
  std::unique_ptr<rtc::Thread> worker_thread_;
  std::unique_ptr<rtc::Thread> signaling_thread_;
  rtc::scoped_refptr<webrtc::PeerConnectionFactoryInterface> factory_;
  rtc::scoped_refptr<webrtc::ConnectionContext> connection_context_;
};

}  // namespace sora

#endif