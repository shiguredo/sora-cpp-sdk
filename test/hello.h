#ifndef TEST_HELLO_H_
#define TEST_HELLO_H_

#include "sora/sora_client_context.h"

struct HelloSoraConfig {
  std::vector<std::string> signaling_urls;
  std::string channel_id;
  std::string role = "sendonly";
  bool video = true;
  bool audio = true;
  int capture_width = 1024;
  int capture_height = 768;
  int video_bit_rate = 0;
  std::string video_codec_type = "H264";
  bool simulcast = false;
  std::optional<bool> ignore_disconnect_websocket;
  std::string client_id;
  std::vector<sora::SoraSignalingConfig::DataChannel> data_channels;
  std::vector<sora::SoraSignalingConfig::ForwardingFilter> forwarding_filters;
};

class HelloSora : public std::enable_shared_from_this<HelloSora>,
                  public sora::SoraSignalingObserver {
 public:
  HelloSora(std::shared_ptr<sora::SoraClientContext> context,
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
    return context_->peer_connection_factory();
  }

 private:
  std::shared_ptr<sora::SoraClientContext> context_;
  HelloSoraConfig config_;
  rtc::scoped_refptr<webrtc::VideoTrackSourceInterface> video_source_;
  rtc::scoped_refptr<webrtc::AudioTrackInterface> audio_track_;
  rtc::scoped_refptr<webrtc::VideoTrackInterface> video_track_;
  std::shared_ptr<sora::SoraSignaling> conn_;
  std::unique_ptr<boost::asio::io_context> ioc_;
};

#endif