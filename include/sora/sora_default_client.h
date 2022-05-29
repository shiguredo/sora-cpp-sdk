#ifndef SORA_SORA_DEFAULT_CLIENT_H_
#define SORA_SORA_DEFAULT_CLIENT_H_

// WebRTC
#include <api/peer_connection_interface.h>

#include "sora/sora_signaling.h"

namespace sora {

struct SoraDefaultClientConfig {
  bool use_audio_deivce = true;
  bool use_hardware_encoder = true;
};

class SoraDefaultClient : public sora::SoraSignalingObserver {
 public:
  SoraDefaultClient(SoraDefaultClientConfig config);
  bool Configure();

  virtual void ConfigureDependencies(
      webrtc::PeerConnectionFactoryDependencies& dependencies) {}
  virtual void OnConfigured() {}

  virtual void* GetAndroidApplicationContext(void* env) { return nullptr; }

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

  rtc::Thread* network_thread() const { return network_thread_.get(); }
  rtc::Thread* worker_thread() const { return worker_thread_.get(); }
  rtc::Thread* signaling_thread() const { return signaling_thread_.get(); }
  rtc::scoped_refptr<webrtc::PeerConnectionFactoryInterface> factory() const {
    return factory_;
  };

 private:
  SoraDefaultClientConfig config_;
  std::unique_ptr<rtc::Thread> network_thread_;
  std::unique_ptr<rtc::Thread> worker_thread_;
  std::unique_ptr<rtc::Thread> signaling_thread_;
  rtc::scoped_refptr<webrtc::PeerConnectionFactoryInterface> factory_;
};

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