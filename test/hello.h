#ifndef TEST_HELLO_H_
#define TEST_HELLO_H_

// WebRTC
#include <absl/memory/memory.h>

#include "sora/sora_signaling.h"

struct HelloSoraConfig {
  std::vector<std::string> signaling_urls;
  std::string channel_id;
  std::string role;
};

class HelloSora : public std::enable_shared_from_this<HelloSora>,
                  public sora::SoraSignalingObserver {
 public:
  HelloSora(HelloSoraConfig config);
  void Init();
  void Run();

  void OnSetOffer() override;
  void OnDisconnect(sora::SoraSignalingErrorCode ec,
                    std::string message) override;
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

#endif