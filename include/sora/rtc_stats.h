#ifndef SORA_RTC_STATS_H_
#define SORA_RTC_STATS_H_

#include <functional>

// WebRTC
#include <api/scoped_refptr.h>
#include <api/stats/rtc_stats_collector_callback.h>
#include <api/stats/rtc_stats_report.h>

namespace sora {

// stats のコールバックを受け取るためのクラス
class RTCStatsCallback : public webrtc::RTCStatsCollectorCallback {
 public:
  typedef std::function<void(
      const webrtc::scoped_refptr<const webrtc::RTCStatsReport>& report)>
      ResultCallback;

  static webrtc::scoped_refptr<RTCStatsCallback> Create(
      ResultCallback result_callback);
  void OnStatsDelivered(
      const webrtc::scoped_refptr<const webrtc::RTCStatsReport>& report)
      override;

 protected:
  RTCStatsCallback(ResultCallback result_callback);
  ~RTCStatsCallback() override = default;

 private:
  ResultCallback result_callback_;
};

}  // namespace sora

#endif
