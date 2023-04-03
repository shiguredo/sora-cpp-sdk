#ifndef SORA_SORA_CLIENT_FACTORY_H_
#define SORA_SORA_CLIENT_FACTORY_H_

// WebRTC
#include <api/peer_connection_interface.h>
#include <media/engine/webrtc_media_engine.h>
#include <pc/connection_context.h>

#include "sora/sora_signaling.h"

namespace sora {

struct SoraClientFactoryConfig {
  // オーディオデバイスを利用するかどうか
  // false にすると一切オーディオデバイスを掴まなくなる
  bool use_audio_device = true;
  // ハードウェアエンコーダ/デコーダを利用するかどうか
  // false にするとソフトウェアエンコーダ/デコーダのみになる（H.264 は利用できない）
  bool use_hardware_encoder = true;

  // MediaEngineDependencies をカスタマイズするためのコールバック関数
  // デフォルトの値が設定された上で、cricket::CreateMediaEngine を生成する直前に呼ばれる
  std::function<void(cricket::MediaEngineDependencies&)>
      configure_media_dependencies;

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

// Sora 向けクライアントを生成するためのクラス
//
// 必要なスレッドの実行や、PeerConnectionFactory の生成を行う。
class SoraClientFactory {
 public:
  static std::shared_ptr<SoraClientFactory> Create(
      const SoraClientFactoryConfig& config);

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
  const SoraClientFactoryConfig& config() const { return config_; }

 private:
  SoraClientFactoryConfig config_;
  std::unique_ptr<rtc::Thread> network_thread_;
  std::unique_ptr<rtc::Thread> worker_thread_;
  std::unique_ptr<rtc::Thread> signaling_thread_;
  rtc::scoped_refptr<webrtc::PeerConnectionFactoryInterface> factory_;
  rtc::scoped_refptr<webrtc::ConnectionContext> connection_context_;
};

}  // namespace sora

#endif