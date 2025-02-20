#include "sora/sora_signaling_whip.h"

#include <regex>

// Boost
#include <boost/algorithm/string/classification.hpp>
#include <boost/algorithm/string/split.hpp>

// WebRTC
#include <media/base/codec_comparators.h>
#include <p2p/client/basic_port_allocator.h>
#include <pc/rtp_media_utils.h>
#include <pc/session_description.h>
#include <rtc_base/crypt_string_revive.h>
#include <rtc_base/crypto_random.h>
#include <rtc_base/proxy_info_revive.h>

#include "sora/data_channel.h"
#include "sora/rtc_ssl_verifier.h"
#include "sora/rtc_stats.h"
#include "sora/session_description.h"
#include "sora/url_parts.h"
#include "sora/version.h"
#include "sora/zlib_helper.h"

namespace sora {

SoraSignalingWhip::SoraSignalingWhip(const SoraSignalingWhipConfig& config)
    : ctx_(CreateSslContext()),
      config_(config),
      resolver_(*config.io_context),
      stream_(*config.io_context, ctx_) {}

SoraSignalingWhip::~SoraSignalingWhip() {
  RTC_LOG(LS_INFO) << "SoraSignaling::~SoraSignaling";
}

std::shared_ptr<SoraSignalingWhip> SoraSignalingWhip::Create(
    const SoraSignalingWhipConfig& config) {
  return std::shared_ptr<SoraSignalingWhip>(new SoraSignalingWhip(config));
}

rtc::scoped_refptr<webrtc::PeerConnectionInterface>
SoraSignalingWhip::GetPeerConnection() const {
  return pc_;
}

void SoraSignalingWhip::Connect() {
  RTC_LOG(LS_INFO) << "SoraSignalingWhip::Connect";

  boost::asio::post(*config_.io_context, [self = shared_from_this()]() {
    if (self->state_ != State::Init && self->state_ != State::Closed) {
      return;
    }

    webrtc::PeerConnectionInterface::RTCConfiguration rtc_config;
    webrtc::PeerConnectionDependencies pc_dependencies(self.get());
    auto result = self->config_.pc_factory->CreatePeerConnectionOrError(
        rtc_config, std::move(pc_dependencies));
    if (!result.ok()) {
      RTC_LOG(LS_ERROR) << "Failed to create PeerConnection: "
                        << result.error().message();
      return;
    }
    auto pc = result.value();
    {
      webrtc::RtpTransceiverInit init;
      init.direction = webrtc::RtpTransceiverDirection::kSendOnly;
      auto transceiver =
          pc->AddTransceiver(cricket::MediaType::MEDIA_TYPE_AUDIO, init);
      if (!transceiver.ok()) {
        RTC_LOG(LS_ERROR) << "Failed to AddTransceiver(audio): error="
                          << transceiver.error().message();
        return;
      }

      auto cap = self->config_.pc_factory->GetRtpSenderCapabilities(
          cricket::MediaType::MEDIA_TYPE_AUDIO);
      std::vector<webrtc::RtpCodecCapability> codecs;
      for (const webrtc::RtpCodecCapability& codec : cap.codecs) {
        if (codec.name == "OPUS") {
          codecs.push_back(codec);
          break;
        }
      }
      transceiver.value()->SetCodecPreferences(codecs);
    }
    webrtc::RtpTransceiverInit video_init;
    if (self->config_.video_source != nullptr) {
      std::string video_track_id = rtc::CreateRandomString(16);
      auto video_track = self->config_.pc_factory->CreateVideoTrack(
          self->config_.video_source, video_track_id);
      auto& init = video_init;
      init.direction = webrtc::RtpTransceiverDirection::kSendOnly;
      init.stream_ids = {rtc::CreateRandomString(16)};
      if (self->config_.send_encodings) {
        init.send_encodings = *self->config_.send_encodings;
      }
      auto transceiver = pc->AddTransceiver(video_track, init);
      if (!transceiver.ok()) {
        RTC_LOG(LS_ERROR) << "Failed to AddTransceiver(video): error="
                          << transceiver.error().message();
        return;
      }

      auto cap = self->config_.pc_factory->GetRtpSenderCapabilities(
          cricket::MediaType::MEDIA_TYPE_VIDEO);
      for (const webrtc::RtpCodecCapability& codec : cap.codecs) {
        RTC_LOG(LS_WARNING) << "codec: " << codec.name;
        for (const auto& param : codec.parameters) {
          RTC_LOG(LS_WARNING) << "  " << param.first << ": " << param.second;
        }
      }
      std::vector<webrtc::RtpCodecCapability> codecs;
      for (const auto& send_encoding : init.send_encodings) {
        RTC_LOG(LS_WARNING)
            << "send_encoding: "
            << (send_encoding.codec ? send_encoding.codec->name : "none");
        for (const webrtc::RtpCodecCapability& codec : cap.codecs) {
          auto codec_format =
              webrtc::SdpVideoFormat(codec.name, codec.parameters);
          if (send_encoding.codec) {
            auto encoding_format = webrtc::SdpVideoFormat(
                send_encoding.codec->name, send_encoding.codec->parameters);
            if (codec_format == encoding_format) {
              RTC_LOG(LS_WARNING) << "match codec: " << codec.name;
              auto it = std::find_if(
                  codecs.begin(), codecs.end(),
                  [&codec_format](const webrtc::RtpCodecCapability& c) {
                    auto format = webrtc::SdpVideoFormat(c.name, c.parameters);
                    return codec_format == format;
                  });
              if (it == codecs.end()) {
                RTC_LOG(LS_WARNING) << "add codec: " << codec.name;
                codecs.push_back(codec);
              }
              break;
            }
          }
        }
      }
      //for (const webrtc::RtpCodecCapability& codec : cap.codecs) {
      //  if (codec.name == "H264") {
      //    codecs.push_back(codec);
      //    break;
      //  }
      //}
      //for (const webrtc::RtpCodecCapability& codec : cap.codecs) {
      //  if (codec.name == "H265") {
      //    codecs.push_back(codec);
      //    break;
      //  }
      //}
      //for (const webrtc::RtpCodecCapability& codec : cap.codecs) {
      //  if (codec.name == "VP9") {
      //    codecs.push_back(codec);
      //    break;
      //  }
      //}
      //for (const webrtc::RtpCodecCapability& codec : cap.codecs) {
      //  if (codec.name == "AV1") {
      //    codecs.push_back(codec);
      //    break;
      //  }
      //}
      for (const webrtc::RtpCodecCapability& codec : cap.codecs) {
        if (codec.name == "rtx") {
          codecs.push_back(codec);
          break;
        }
      }
      transceiver.value()->SetCodecPreferences(codecs);
    }

    pc->CreateOffer(
        CreateSessionDescriptionThunk::Create(
            [self,
             video_init](webrtc::SessionDescriptionInterface* description) {
              auto offer = std::unique_ptr<webrtc::SessionDescriptionInterface>(
                  description);

              // 各 RtpEncodingParameters の利用するコーデックと payload_type を関連付ける
              std::map<std::string, int> rid_payload_type_map;
              auto& content = offer->description()->contents()[1];
              auto media_desc = content.media_description();
              for (auto& send_encoding : video_init.send_encodings) {
                RTC_LOG(LS_WARNING)
                    << "send_encoding: " << send_encoding.codec->name;
                for (auto& codec : media_desc->codecs()) {
                  RTC_LOG(LS_WARNING) << "codec: " << codec.name;
                  if (send_encoding.codec &&
                      webrtc::IsSameRtpCodec(codec, *send_encoding.codec)) {
                    RTC_LOG(LS_WARNING) << "rid=" << send_encoding.rid
                                        << " codec=" << codec.name
                                        << " payload_type=" << codec.id;
                    rid_payload_type_map[send_encoding.rid] = codec.id;
                  }
                }
              }
              auto& track = media_desc->mutable_streams()[0];
              auto rids = track.rids();
              for (auto& rid : rids) {
                //if (rid.rid == "r0" || rid.rid == "r1") {
                //  continue;
                //}
                auto it = rid_payload_type_map.find(rid.rid);
                if (it == rid_payload_type_map.end()) {
                  continue;
                }
                rid.payload_types.push_back(it->second);
              }
              track.set_rids(rids);

              std::string offer_sdp;
              if (!offer->ToString(&offer_sdp)) {
                RTC_LOG(LS_ERROR) << "Failed to get SDP";
                return;
              }
              RTC_LOG(LS_INFO) << "Offer SDP: " << offer_sdp;

              boost::asio::post(*self->config_.io_context, [self, offer_sdp,
                                                            video_init]() {
                URLParts parts;
                if (!URLParts::Parse(self->config_.signaling_url, parts)) {
                  RTC_LOG(LS_ERROR)
                      << "Failed to parse url: " << self->config_.signaling_url;
                  return;
                }

                self->req_.target(parts.path_query_fragment + "/" +
                                  self->config_.channel_id +
                                  "?video_bit_rate=6000");
                self->req_.method(boost::beast::http::verb::post);
                // self->req_.set(boost::beast::http::field::authorization, "Bearer " + self->config_.secret_key);
                self->req_.set(boost::beast::http::field::content_type,
                               "application/sdp");
                self->req_.set(boost::beast::http::field::user_agent,
                               BOOST_BEAST_VERSION_STRING);
                self->req_.body() = offer_sdp;
                self->req_.prepare_payload();
                RTC_LOG(LS_INFO) << "Send request to: " << self->req_.target();
                self->SendRequest([self, offer_sdp,
                                   video_init](boost::beast::error_code ec) {
                  if (ec) {
                    return;
                  }

                  // link ヘッダーはこんな感じの文字列になってる（見やすさのために改行を入れているが実際は含まない）
                  //
                  // <turn:192.168.0.1:38536?transport=udp>; rel="ice-server"; username="F3OJ7m2d"; credential="6JswLI5cPuTF8vL5Q1f40tQx7MusPWW0"; credential-type="password",
                  // <turn:192.168.0.1:3478?transport=tcp>; rel="ice-server"; username="F3OJ7m2d"; credential="6JswLI5cPuTF8vL5Q1f40tQx7MusPWW0"; credential-type="password"
                  auto link = self->res_[boost::beast::http::field::link];
                  if (link.empty()) {
                    RTC_LOG(LS_ERROR) << "No Link header";
                    return;
                  }
                  std::vector<std::string> strs;
                  boost::algorithm::split(strs, link, boost::is_any_of(","));

                  webrtc::PeerConnectionInterface::IceServer server;
                  for (const auto& str : strs) {
                    std::smatch m;
                    if (!std::regex_search(str.begin(), str.end(), m,
                                           std::regex(R"(<([^>]+)>)"))) {
                      RTC_LOG(LS_ERROR) << "Failed to match <...>: str=" << str;
                      return;
                    }
                    server.urls.push_back(m[1].str());
                    if (!std::regex_search(
                            str.begin(), str.end(), m,
                            std::regex(R"|(username="([^"]+)")|"))) {
                      RTC_LOG(LS_ERROR)
                          << "Failed to match username=\"...\": str=" << str;
                      return;
                    }
                    server.username = m[1].str();
                    if (!std::regex_search(
                            str.begin(), str.end(), m,
                            std::regex(R"|(credential="([^"]+)")|"))) {
                      RTC_LOG(LS_ERROR)
                          << "Failed to match credential=\"...\": str=" << str;
                      return;
                    }
                    server.password = m[1].str();
                    RTC_LOG(LS_INFO) << "Server: url=" << server.urls.back()
                                     << ", username=" << server.username
                                     << ", password=" << server.password;
                  }
                  webrtc::PeerConnectionInterface::RTCConfiguration config;
                  config.servers.push_back(server);
                  config.type = webrtc::PeerConnectionInterface::
                      IceTransportsType::kRelay;
                  self->pc_->SetConfiguration(config);

                  auto offer = webrtc::CreateSessionDescription(
                      webrtc::SdpType::kOffer, offer_sdp);
                  self->pc_->SetLocalDescription(
                      SetSessionDescriptionThunk::Create(
                          [self, video_init]() {
                            auto answer = webrtc::CreateSessionDescription(
                                webrtc::SdpType::kAnswer, self->res_.body());
                            self->pc_->SetRemoteDescription(
                                SetSessionDescriptionThunk::Create(
                                    [self, video_init]() {
                                      RTC_LOG(LS_INFO)
                                          << "Succeeded to "
                                             "SetRemoteDescription";
                                      auto p = self->pc_->GetSenders()[1]
                                                   ->GetParameters();
                                      for (int i = 0; i < p.encodings.size();
                                           i++) {
                                        p.encodings[i].codec =
                                            video_init.send_encodings[i].codec;
                                        p.encodings[i].scalability_mode =
                                            video_init.send_encodings[i]
                                                .scalability_mode;
                                      }
                                      self->pc_->GetSenders()[1]->SetParameters(
                                          p);
                                    },
                                    [](webrtc::RTCError error) {
                                      RTC_LOG(LS_ERROR)
                                          << "Failed to SetRemoteDescription";
                                    })
                                    .get(),
                                answer.release());
                          },
                          [](webrtc::RTCError error) {
                            RTC_LOG(LS_ERROR)
                                << "Failed to SetLocalDescription";
                          })
                          .get(),
                      offer.release());
                });
              });
            },
            [self](webrtc::RTCError error) {
              RTC_LOG(LS_ERROR)
                  << "Failed to CreateOffer: error=" << error.message();
              self->pc_ = nullptr;
              self->state_ = State::Closed;
            })
            .get(),
        webrtc::PeerConnectionInterface::RTCOfferAnswerOptions());

    self->pc_ = pc;
    self->state_ = State::Connecting;
  });
}

void SoraSignalingWhip::SendRequest(
    std::function<void(boost::beast::error_code)> on_response) {
  RTC_LOG(LS_INFO) << "SoraSignalingWhip::SendRequest";

  URLParts parts;
  if (!URLParts::Parse(config_.signaling_url, parts)) {
    RTC_LOG(LS_ERROR) << "Failed to parse url: " << config_.signaling_url;
    on_response(boost::beast::error_code(boost::beast::errc::invalid_argument,
                                         boost::beast::system_category()));
    return;
  }

  // Set SNI Hostname (many hosts need this to handshake successfully)
  if (!SSL_set_tlsext_host_name(stream_.native_handle(), parts.host.c_str())) {
    boost::beast::error_code ec{static_cast<int>(::ERR_get_error()),
                                boost::asio::error::get_ssl_category()};
    RTC_LOG(LS_ERROR) << "Failed to SSL_set_tlsext_host_name: ec="
                      << ec.message();
    on_response(ec);
    return;
  }

  req_.set(boost::beast::http::field::host, parts.host);

  resolver_.async_resolve(
      parts.host, parts.GetPort(),
      [self = shared_from_this(), on_response](
          boost::beast::error_code ec,
          boost::asio::ip::tcp::resolver::results_type results) {
        if (ec) {
          RTC_LOG(LS_ERROR) << "Failed to async_resolve: ec=" << ec.message();
          on_response(ec);
          return;
        }

        self->stream_.next_layer().expires_after(std::chrono::seconds(10));
        self->stream_.next_layer().async_connect(
            results,
            [self, on_response](
                boost::beast::error_code ec,
                boost::asio::ip::tcp::resolver::results_type::endpoint_type
                    endpoint_type) {
              if (ec) {
                RTC_LOG(LS_ERROR)
                    << "Failed to async_connect: ec=" << ec.message();
                on_response(ec);
                return;
              }

              self->stream_.async_handshake(
                  boost::asio::ssl::stream_base::client,
                  [self, on_response](boost::beast::error_code ec) {
                    if (ec) {
                      RTC_LOG(LS_ERROR)
                          << "Failed to async_handshake: ec=" << ec.message();
                      on_response(ec);
                      return;
                    }

                    self->stream_.next_layer().expires_after(
                        std::chrono::seconds(10));
                    boost::beast::http::async_write(
                        self->stream_, self->req_,
                        [self, on_response](boost::beast::error_code ec,
                                            std::size_t bytes_transferred) {
                          if (ec) {
                            RTC_LOG(LS_ERROR)
                                << "Failed to async_write: ec=" << ec.message();
                            on_response(ec);
                            return;
                          }

                          boost::beast::http::async_read(
                              self->stream_, self->buffer_, self->res_,
                              [self, on_response](
                                  boost::beast::error_code ec,
                                  std::size_t bytes_transferred) {
                                if (ec) {
                                  RTC_LOG(LS_ERROR)
                                      << "Failed to async_read: ec="
                                      << ec.message();
                                  on_response(ec);
                                  return;
                                }

                                RTC_LOG(LS_INFO) << "Response: " << self->res_;

                                self->stream_.async_shutdown(
                                    [self,
                                     on_response](boost::beast::error_code ec) {
                                      if (ec && ec != boost::asio::ssl::error::
                                                          stream_truncated) {
                                        RTC_LOG(LS_ERROR)
                                            << "Failed to async_shutdown: "
                                            << ec.message();
                                      }

                                      if (boost::beast::http::to_status_class(
                                              self->res_.result()) !=
                                          boost::beast::http::status_class::
                                              successful) {
                                        RTC_LOG(LS_ERROR)
                                            << "Failed to request: status="
                                            << self->res_.result_int();
                                        on_response(boost::beast::error_code(
                                            boost::beast::errc::
                                                invalid_argument,
                                            boost::beast::system_category()));
                                        return;
                                      }

                                      // Successful
                                      on_response(boost::beast::error_code());
                                    });
                              });
                        });
                  });
            });
      });
}

void SoraSignalingWhip::Disconnect() {
  boost::asio::post(*config_.io_context, [self = shared_from_this()]() {
    if (self->state_ == State::Init) {
      self->state_ = State::Closed;
      return;
    }
  });
}

boost::asio::ssl::context SoraSignalingWhip::CreateSslContext() {
  boost::asio::ssl::context ctx{boost::asio::ssl::context::tlsv12_client};
  ctx.set_verify_mode(boost::asio::ssl::verify_none);
  return ctx;
}
}  // namespace sora
