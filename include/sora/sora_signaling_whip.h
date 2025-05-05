#ifndef SORA_SORA_SIGNALING_WHIP_H_INCLUDED
#define SORA_SORA_SIGNALING_WHIP_H_INCLUDED

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

// Boost
#include <boost/asio/ssl.hpp>
#include <boost/beast.hpp>
#include <boost/json.hpp>
#include <boost/optional.hpp>

// WebRTC
#include <api/media_stream_interface.h>
#include <api/peer_connection_interface.h>
#include <api/scoped_refptr.h>

namespace sora {

class SoraSignalingWhipObserver {
 public:
};

struct SoraSignalingWhipConfig {
  boost::asio::io_context* io_context;
  rtc::scoped_refptr<webrtc::PeerConnectionFactoryInterface> pc_factory;
  std::weak_ptr<SoraSignalingWhipObserver> observer;

  std::string signaling_url;
  std::string channel_id;
  std::optional<std::vector<webrtc::RtpEncodingParameters>> send_encodings;
  rtc::scoped_refptr<webrtc::VideoTrackSourceInterface> video_source;
};

class SoraSignalingWhip
    : public std::enable_shared_from_this<SoraSignalingWhip>,
      public webrtc::PeerConnectionObserver {
  SoraSignalingWhip(const SoraSignalingWhipConfig& config);

 public:
  ~SoraSignalingWhip();
  static std::shared_ptr<SoraSignalingWhip> Create(
      const SoraSignalingWhipConfig& config);
  rtc::scoped_refptr<webrtc::PeerConnectionInterface> GetPeerConnection() const;

  void Connect();
  void Disconnect();

 private:
  void SendRequest(std::function<void(boost::beast::error_code)> on_response);
  static boost::asio::ssl::context CreateSslContext();

  // webrtc::PeerConnectionObserver の実装
 private:
  void OnSignalingChange(
      webrtc::PeerConnectionInterface::SignalingState new_state) override {}
  void OnDataChannel(
      rtc::scoped_refptr<webrtc::DataChannelInterface> data_channel) override {}
  void OnStandardizedIceConnectionChange(
      webrtc::PeerConnectionInterface::IceConnectionState new_state) override {}
  void OnConnectionChange(
      webrtc::PeerConnectionInterface::PeerConnectionState new_state) override {
  }
  void OnIceGatheringChange(
      webrtc::PeerConnectionInterface::IceGatheringState new_state) override {}
  void OnIceCandidate(const webrtc::IceCandidateInterface* candidate) override {
  }
  void OnIceCandidateError(const std::string& address,
                           int port,
                           const std::string& url,
                           int error_code,
                           const std::string& error_text) override {}
  void OnTrack(rtc::scoped_refptr<webrtc::RtpTransceiverInterface> transceiver)
      override {}
  void OnRemoveTrack(
      rtc::scoped_refptr<webrtc::RtpReceiverInterface> receiver) override {}

 private:
  SoraSignalingWhipConfig config_;

  rtc::scoped_refptr<webrtc::PeerConnectionInterface> pc_;

  enum State {
    Init,
    Connecting,
    Connected,
    Closing,
    Closed,
  };

  State state_ = State::Init;

  boost::asio::ssl::context ctx_;
  boost::asio::ip::tcp::resolver resolver_;
  boost::asio::ssl::stream<boost::beast::tcp_stream> stream_;
  boost::beast::flat_buffer buffer_;
  boost::beast::http::request<boost::beast::http::string_body> req_;
  boost::beast::http::response<boost::beast::http::string_body> res_;
};

}  // namespace sora

#endif
