#ifndef SORA_SESSION_DESCRIPTION_H_
#define SORA_SESSION_DESCRIPTION_H_

#include <functional>

// WebRTC
#include <api/peer_connection_interface.h>

namespace sora {

typedef std::function<void(webrtc::SessionDescriptionInterface*)>
    OnSessionCreateSuccessFunc;
typedef std::function<void(webrtc::RTCError)> OnSessionCreateFailureFunc;
typedef std::function<void()> OnSessionSetSuccessFunc;
typedef std::function<void(webrtc::RTCError)> OnSessionSetFailureFunc;

// CreateSessionDescriptionObserver のコールバックを関数オブジェクトで扱えるようにするためのクラス
class CreateSessionDescriptionThunk
    : public webrtc::CreateSessionDescriptionObserver {
 public:
  typedef OnSessionCreateSuccessFunc OnSuccessFunc;
  typedef OnSessionCreateFailureFunc OnFailureFunc;

  static webrtc::scoped_refptr<CreateSessionDescriptionThunk> Create(
      OnSuccessFunc on_success,
      OnFailureFunc on_failure);

 protected:
  CreateSessionDescriptionThunk(OnSuccessFunc on_success,
                                OnFailureFunc on_failure);
  void OnSuccess(webrtc::SessionDescriptionInterface* desc) override;
  void OnFailure(webrtc::RTCError error) override;

 private:
  OnSuccessFunc on_success_;
  OnFailureFunc on_failure_;
};

// SetSessionDescriptionObserver のコールバックを関数オブジェクトで扱えるようにするためのクラス
class SetSessionDescriptionThunk
    : public webrtc::SetSessionDescriptionObserver {
 public:
  typedef OnSessionSetSuccessFunc OnSuccessFunc;
  typedef OnSessionSetFailureFunc OnFailureFunc;

  static webrtc::scoped_refptr<SetSessionDescriptionThunk> Create(
      OnSuccessFunc on_success,
      OnFailureFunc on_failure);

 protected:
  SetSessionDescriptionThunk(OnSuccessFunc on_success,
                             OnFailureFunc on_failure);
  void OnSuccess() override;
  void OnFailure(webrtc::RTCError error) override;

 private:
  OnSuccessFunc on_success_;
  OnFailureFunc on_failure_;
};

class SessionDescription {
 public:
  static void SetOffer(webrtc::PeerConnectionInterface* pc,
                       const std::string sdp,
                       OnSessionSetSuccessFunc on_success,
                       OnSessionSetFailureFunc on_failure);

  static void CreateAnswer(webrtc::PeerConnectionInterface* pc,
                           OnSessionCreateSuccessFunc on_success,
                           OnSessionCreateFailureFunc on_failure);

  static void SetAnswer(webrtc::PeerConnectionInterface* pc,
                        const std::string sdp,
                        OnSessionSetSuccessFunc on_success,
                        OnSessionSetFailureFunc on_failure);
};

}  // namespace sora

#endif
