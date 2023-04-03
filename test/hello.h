#ifndef TEST_HELLO_H_
#define TEST_HELLO_H_

#include "sora/sora_client_factory.h"

struct HelloSoraConfig {
  std::vector<std::string> signaling_urls;
  std::string channel_id;
  std::string role;
  enum class Mode {
    Hello,
    Lyra,
  };
  Mode mode = Mode::Hello;
};

class HelloSora : public std::enable_shared_from_this<HelloSora>,
                  public sora::SoraSignalingObserver {
 public:
  HelloSora(std::shared_ptr<sora::SoraClientFactory> factory,
            HelloSoraConfig config);
  ~HelloSora();

  void Run();

  // sora::SoraSignalingObserver
  void OnSetOffer(std::string offer) override;
  void OnDisconnect(sora::SoraSignalingErrorCode ec,
                    std::string message) override;
  void OnNotify(std::string text) override {}
  void OnPush(std::string text) override {}
  void OnMessage(std::string label, std::string data) override {}

  void OnTrack(rtc::scoped_refptr<webrtc::RtpTransceiverInterface> transceiver)
      override {}
  void OnRemoveTrack(
      rtc::scoped_refptr<webrtc::RtpReceiverInterface> receiver) override {}

  void OnDataChannel(std::string label) override {}

 private:
  rtc::scoped_refptr<webrtc::PeerConnectionFactoryInterface> pc_factory() {
    return factory_->peer_connection_factory();
  }

 private:
  std::shared_ptr<sora::SoraClientFactory> factory_;
  HelloSoraConfig config_;
  rtc::scoped_refptr<webrtc::VideoTrackSourceInterface> video_source_;
  rtc::scoped_refptr<webrtc::AudioTrackInterface> audio_track_;
  rtc::scoped_refptr<webrtc::VideoTrackInterface> video_track_;
  std::shared_ptr<sora::SoraSignaling> conn_;
  std::unique_ptr<boost::asio::io_context> ioc_;
};

#endif