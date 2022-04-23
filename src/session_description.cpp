#include "sora/session_description.h"

namespace sora {

rtc::scoped_refptr<CreateSessionDescriptionThunk>
CreateSessionDescriptionThunk::Create(OnSuccessFunc on_success,
                                      OnFailureFunc on_failure) {
  return rtc::make_ref_counted<CreateSessionDescriptionThunk>(
      std::move(on_success), std::move(on_failure));
}

CreateSessionDescriptionThunk::CreateSessionDescriptionThunk(
    OnSuccessFunc on_success,
    OnFailureFunc on_failure)
    : on_success_(std::move(on_success)), on_failure_(std::move(on_failure)) {}
void CreateSessionDescriptionThunk::OnSuccess(
    webrtc::SessionDescriptionInterface* desc) {
  auto f = std::move(on_success_);
  if (f) {
    f(desc);
  }
}
void CreateSessionDescriptionThunk::OnFailure(webrtc::RTCError error) {
  RTC_LOG(LS_ERROR) << "Failed to create session description : "
                    << webrtc::ToString(error.type()) << ": "
                    << error.message();
  auto f = std::move(on_failure_);
  if (f) {
    f(error);
  }
}

rtc::scoped_refptr<SetSessionDescriptionThunk>
SetSessionDescriptionThunk::Create(OnSuccessFunc on_success,
                                   OnFailureFunc on_failure) {
  return rtc::make_ref_counted<SetSessionDescriptionThunk>(
      std::move(on_success), std::move(on_failure));
}

SetSessionDescriptionThunk::SetSessionDescriptionThunk(OnSuccessFunc on_success,
                                                       OnFailureFunc on_failure)
    : on_success_(std::move(on_success)), on_failure_(std::move(on_failure)) {}
void SetSessionDescriptionThunk::OnSuccess() {
  auto f = std::move(on_success_);
  if (f) {
    f();
  }
}
void SetSessionDescriptionThunk::OnFailure(webrtc::RTCError error) {
  RTC_LOG(LS_ERROR) << "Failed to set session description : "
                    << webrtc::ToString(error.type()) << ": "
                    << error.message();
  auto f = std::move(on_failure_);
  if (f) {
    f(error);
  }
}

void SessionDescription::SetOffer(webrtc::PeerConnectionInterface* pc,
                                  const std::string sdp,
                                  OnSessionSetSuccessFunc on_success,
                                  OnSessionSetFailureFunc on_failure) {
  webrtc::SdpParseError error;
  std::unique_ptr<webrtc::SessionDescriptionInterface> session_description =
      webrtc::CreateSessionDescription(webrtc::SdpType::kOffer, sdp, &error);
  if (!session_description) {
    RTC_LOG(LS_ERROR) << "Failed to create session description: "
                      << error.description.c_str()
                      << "\nline: " << error.line.c_str();
    on_failure(webrtc::RTCError(webrtc::RTCErrorType::SYNTAX_ERROR,
                                error.description));
    return;
  }
  pc->SetRemoteDescription(SetSessionDescriptionThunk::Create(
                               std::move(on_success), std::move(on_failure)),
                           session_description.release());
}

void SessionDescription::CreateAnswer(webrtc::PeerConnectionInterface* pc,
                                      OnSessionCreateSuccessFunc on_success,
                                      OnSessionCreateFailureFunc on_failure) {
  rtc::scoped_refptr<webrtc::PeerConnectionInterface> rpc(pc);
  auto with_set_local_desc = [rpc, on_success = std::move(on_success)](
                                 webrtc::SessionDescriptionInterface* desc) {
    std::string sdp;
    desc->ToString(&sdp);
    RTC_LOG(LS_INFO) << "Created session description : " << sdp;
    rpc->SetLocalDescription(
        SetSessionDescriptionThunk::Create(nullptr, nullptr), desc);
    if (on_success) {
      on_success(desc);
    }
  };
  rpc->CreateAnswer(CreateSessionDescriptionThunk::Create(
                        std::move(with_set_local_desc), std::move(on_failure)),
                    webrtc::PeerConnectionInterface::RTCOfferAnswerOptions());
}

void SessionDescription::SetAnswer(webrtc::PeerConnectionInterface* pc,
                                   const std::string sdp,
                                   OnSessionSetSuccessFunc on_success,
                                   OnSessionSetFailureFunc on_failure) {
  webrtc::SdpParseError error;
  std::unique_ptr<webrtc::SessionDescriptionInterface> session_description =
      webrtc::CreateSessionDescription(webrtc::SdpType::kAnswer, sdp, &error);
  if (!session_description) {
    RTC_LOG(LS_ERROR) << __FUNCTION__
                      << "Failed to create session description: "
                      << error.description.c_str()
                      << "\nline: " << error.line.c_str();
    on_failure(webrtc::RTCError(webrtc::RTCErrorType::SYNTAX_ERROR,
                                error.description));
    return;
  }
  pc->SetRemoteDescription(SetSessionDescriptionThunk::Create(
                               std::move(on_success), std::move(on_failure)),
                           session_description.release());
}

}  // namespace sora
