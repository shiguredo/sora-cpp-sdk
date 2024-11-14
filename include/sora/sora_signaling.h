#ifndef SORA_SORA_SIGNALING_H_INCLUDED
#define SORA_SORA_SIGNALING_H_INCLUDED

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

// Boost
#include <boost/json.hpp>
#include <boost/optional.hpp>

// WebRTC
#include <api/media_stream_interface.h>
#include <api/peer_connection_interface.h>
#include <api/scoped_refptr.h>

#include "sora/data_channel.h"
#include "sora/version.h"
#include "sora/websocket.h"

namespace sora {

extern const char kActionBlock[];
extern const char kActionAllow[];
extern const char kFieldConnectionId[];
extern const char kFieldClientId[];
extern const char kFieldKind[];
extern const char kOperatorIsIn[];
extern const char kOperatorIsNotIn[];

enum class SoraSignalingErrorCode {
  CLOSE_SUCCEEDED,
  CLOSE_FAILED,
  INTERNAL_ERROR,
  INVALID_PARAMETER,
  WEBSOCKET_HANDSHAKE_FAILED,
  WEBSOCKET_ONCLOSE,
  WEBSOCKET_ONERROR,
  PEER_CONNECTION_STATE_FAILED,
  ICE_FAILED,
};

enum class SoraSignalingType {
  WEBSOCKET,
  DATACHANNEL,
};

enum class SoraSignalingDirection {
  SENT,
  RECEIVED,
};

class SoraSignalingObserver {
 public:
  virtual void OnSetOffer(std::string offer) = 0;
  virtual void OnDisconnect(SoraSignalingErrorCode ec, std::string message) = 0;
  virtual void OnNotify(std::string text) = 0;
  virtual void OnPush(std::string text) = 0;
  virtual void OnMessage(std::string label, std::string data) = 0;
  virtual void OnSwitched(std::string text) {}
  virtual void OnSignalingMessage(SoraSignalingType type,
                                  SoraSignalingDirection direction,
                                  std::string message) {}
  virtual void OnWsClose(uint16_t code, std::string message) {}

  virtual void OnTrack(
      rtc::scoped_refptr<webrtc::RtpTransceiverInterface> transceiver) = 0;
  virtual void OnRemoveTrack(
      rtc::scoped_refptr<webrtc::RtpReceiverInterface> receiver) = 0;

  virtual void OnDataChannel(std::string label) = 0;
};

struct SoraSignalingConfig {
  boost::asio::io_context* io_context;
  rtc::scoped_refptr<webrtc::PeerConnectionFactoryInterface> pc_factory;
  std::weak_ptr<SoraSignalingObserver> observer;

  std::vector<std::string> signaling_urls;
  std::string channel_id;
  std::string client_id;
  std::string bundle_id;

  std::string sora_client;

  bool insecure = false;
  bool video = true;
  bool audio = true;
  std::string video_codec_type = "";
  std::string audio_codec_type = "";
  int video_bit_rate = 0;
  int audio_bit_rate = 0;
  boost::json::value video_vp9_params;
  boost::json::value video_av1_params;
  boost::json::value video_h264_params;
  boost::json::value video_h265_params;
  boost::json::value audio_opus_params;
  std::string audio_streaming_language_code;
  boost::json::value metadata;
  boost::json::value signaling_notify_metadata;
  std::string role = "sendonly";
  std::optional<bool> multistream;
  std::optional<bool> spotlight;
  int spotlight_number = 0;
  std::string spotlight_focus_rid;
  std::string spotlight_unfocus_rid;
  std::optional<bool> simulcast;
  std::optional<bool> simulcast_multicodec;
  std::string simulcast_rid;
  std::optional<bool> data_channel_signaling;
  int data_channel_signaling_timeout = 180;
  std::optional<bool> ignore_disconnect_websocket;
  int disconnect_wait_timeout = 5;
  struct DataChannel {
    std::string label;
    std::string direction;
    std::optional<bool> ordered;
    std::optional<int32_t> max_packet_life_time;
    std::optional<int32_t> max_retransmits;
    std::optional<std::string> protocol;
    std::optional<bool> compress;
    std::optional<std::vector<boost::json::value>> header;
  };
  std::vector<DataChannel> data_channels;

  struct ForwardingFilter {
    std::optional<std::string> name;
    std::optional<int> priority;
    std::optional<std::string> action;
    struct Rule {
      std::string field;
      std::string op;
      std::vector<std::string> values;
    };
    std::vector<std::vector<Rule>> rules;
    std::optional<std::string> version;
    std::optional<boost::json::value> metadata;
  };
  std::optional<ForwardingFilter> forwarding_filter;
  std::optional<std::vector<ForwardingFilter>> forwarding_filters;

  std::optional<std::string> client_cert;
  std::optional<std::string> client_key;
  std::optional<std::string> ca_cert;

  int websocket_close_timeout = 3;
  int websocket_connection_timeout = 30;

  std::string proxy_url;
  std::string proxy_username;
  std::string proxy_password;
  std::string proxy_agent;
  // proxy を設定する場合は必須
  rtc::NetworkManager* network_manager = nullptr;
  rtc::PacketSocketFactory* socket_factory = nullptr;

  bool disable_signaling_url_randomization = false;

  std::optional<http_header_value> user_agent;
};

class SoraSignaling : public std::enable_shared_from_this<SoraSignaling>,
                      public webrtc::PeerConnectionObserver,
                      public DataChannelObserver {
  SoraSignaling(const SoraSignalingConfig& config);

 public:
  ~SoraSignaling();
  static std::shared_ptr<SoraSignaling> Create(
      const SoraSignalingConfig& config);
  rtc::scoped_refptr<webrtc::PeerConnectionInterface> GetPeerConnection() const;
  std::string GetVideoMid() const;
  std::string GetAudioMid() const;

  void Connect();
  void Disconnect();
  bool SendDataChannel(const std::string& label, const std::string& data);

  std::string GetConnectionID() const;
  std::string GetSelectedSignalingURL() const;
  std::string GetConnectedSignalingURL() const;
  bool IsConnectedDataChannel() const;
  bool IsConnectedWebsocket() const;

 private:
  static bool ParseURL(const std::string& url, URLParts& parts, bool& ssl);

  void Redirect(std::string url);
  void OnRedirect(boost::system::error_code ec,
                  std::string url,
                  std::shared_ptr<Websocket> ws);

  bool CheckSdp(const std::string& sdp);

  void DoRead();
  void DoSendConnect(bool redirect);
  void DoSendPong();
  void DoSendPong(
      const rtc::scoped_refptr<const webrtc::RTCStatsReport>& report);
  void DoSendUpdate(const std::string& sdp, std::string type);

  rtc::scoped_refptr<webrtc::PeerConnectionInterface> CreatePeerConnection(
      boost::json::value jconfig);

 private:
  // 出来るだけサーバに type: disconnect を送ってから閉じる
  // force_error_code が設定されていない場合、NO-ERROR でサーバに送信し、ユーザにはデフォルトのエラーコードとメッセージでコールバックする。reason や message は無視される。
  // force_error_code が設定されている場合、reason でサーバに送信し、指定されたエラーコードと、message にデフォルトのメッセージを追加したメッセージでコールバックする。
  void DoInternalDisconnect(
      std::optional<SoraSignalingErrorCode> force_error_code,
      std::string reason,
      std::string message);

  std::function<void(webrtc::RTCError)> CreateIceError(std::string message);

  void OnConnect(boost::system::error_code ec,
                 std::string url,
                 std::shared_ptr<Websocket> ws);
  void OnRead(boost::system::error_code ec,
              std::size_t bytes_transferred,
              std::string text);
  void DoConnect();

 private:
  void SetEncodingParameters(
      std::string mid,
      std::vector<webrtc::RtpEncodingParameters> encodings);
  void ResetEncodingParameters();

  void WsWriteSignaling(std::string text, Websocket::write_callback_t on_write);
  void SendOnDisconnect(SoraSignalingErrorCode ec, std::string message);
  void SendOnSignalingMessage(SoraSignalingType type,
                              SoraSignalingDirection direction,
                              std::string message);
  void SendOnWsClose(const boost::beast::websocket::close_reason& reason);
  void SendSelfOnWsClose(boost::system::error_code ec);

  webrtc::DataBuffer ConvertToDataBuffer(const std::string& label,
                                         const std::string& input);

  void Clear();

  // webrtc::PeerConnectionObserver の実装
 private:
  void OnSignalingChange(
      webrtc::PeerConnectionInterface::SignalingState new_state) override {}
  void OnDataChannel(
      rtc::scoped_refptr<webrtc::DataChannelInterface> data_channel) override;
  void OnStandardizedIceConnectionChange(
      webrtc::PeerConnectionInterface::IceConnectionState new_state) override;
  void OnConnectionChange(
      webrtc::PeerConnectionInterface::PeerConnectionState new_state) override;
  void OnIceGatheringChange(
      webrtc::PeerConnectionInterface::IceGatheringState new_state) override {}
  void OnIceCandidate(const webrtc::IceCandidateInterface* candidate) override;
  void OnIceCandidateError(const std::string& address,
                           int port,
                           const std::string& url,
                           int error_code,
                           const std::string& error_text) override;
  void OnTrack(
      rtc::scoped_refptr<webrtc::RtpTransceiverInterface> transceiver) override;
  void OnRemoveTrack(
      rtc::scoped_refptr<webrtc::RtpReceiverInterface> receiver) override;

  // DataChannelObserver の実装
 private:
  void OnStateChange(
      rtc::scoped_refptr<webrtc::DataChannelInterface> data_channel) override;
  void OnMessage(rtc::scoped_refptr<webrtc::DataChannelInterface> data_channel,
                 const webrtc::DataBuffer& buffer) override;

 private:
  SoraSignalingConfig config_;

  struct OfferConfig {
    bool multistream = false;
    bool simulcast = false;
    bool spotlight = false;
  };
  OfferConfig offer_config_;

  std::string connection_id_;
  std::vector<std::shared_ptr<Websocket>> connecting_wss_;
  struct atomic_string {
    std::string load() const {
      std::lock_guard<std::mutex> lock(m);
      return s;
    }
    void store(std::string s) {
      std::lock_guard<std::mutex> lock(m);
      this->s = s;
    }

   private:
    std::string s;
    mutable std::mutex m;
  };
  atomic_string selected_signaling_url_;
  atomic_string connected_signaling_url_;
  std::shared_ptr<Websocket> ws_;
  std::shared_ptr<DataChannel> dc_;
  bool using_datachannel_ = false;
  bool ws_connected_ = false;
  struct DataChannelInfo {
    bool compressed = false;
    bool notified = false;
  };
  std::map<std::string, DataChannelInfo> dc_labels_;

  rtc::scoped_refptr<webrtc::PeerConnectionInterface> pc_;
  std::vector<webrtc::RtpEncodingParameters> encodings_;
  std::string video_mid_;
  std::string audio_mid_;

  boost::asio::deadline_timer connection_timeout_timer_;
  boost::asio::deadline_timer closing_timeout_timer_;
  std::function<void(boost::system::error_code ec)> on_ws_close_;
  webrtc::PeerConnectionInterface::IceConnectionState ice_state_ =
      webrtc::PeerConnectionInterface::kIceConnectionNew;
  webrtc::PeerConnectionInterface::PeerConnectionState connection_state_ =
      webrtc::PeerConnectionInterface::PeerConnectionState::kNew;

  enum State {
    Init,
    Connecting,
    Redirecting,
    Connected,
    Closing,
    Closed,
  };

  State state_ = State::Init;
};

}  // namespace sora

#endif
