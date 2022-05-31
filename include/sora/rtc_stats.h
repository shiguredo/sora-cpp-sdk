#ifndef SORA_RTC_STATS_H_
#define SORA_RTC_STATS_H_

// WebRTC
#include <api/peer_connection_interface.h>
#include <api/scoped_refptr.h>
#include <rtc_base/ref_counted_object.h>

namespace sora {

// stats のコールバックを受け取るためのクラス
class RTCStatsCallback : public webrtc::RTCStatsCollectorCallback {
 public:
  typedef std::function<void(
      const rtc::scoped_refptr<const webrtc::RTCStatsReport>& report)>
      ResultCallback;

  static rtc::scoped_refptr<RTCStatsCallback> Create(
      ResultCallback result_callback);
  void OnStatsDelivered(
      const rtc::scoped_refptr<const webrtc::RTCStatsReport>& report) override;

 protected:
  RTCStatsCallback(ResultCallback result_callback);
  ~RTCStatsCallback() override = default;

 private:
  ResultCallback result_callback_;
};

}  // namespace sora

#endif
