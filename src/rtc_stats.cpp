#include "sora/rtc_stats.h"

#include <utility>

// WebRTC
#include <api/make_ref_counted.h>
#include <api/scoped_refptr.h>
#include <api/stats/rtc_stats_report.h>

namespace sora {

webrtc::scoped_refptr<RTCStatsCallback> RTCStatsCallback::Create(
    ResultCallback result_callback) {
  return webrtc::make_ref_counted<RTCStatsCallback>(std::move(result_callback));
}

void RTCStatsCallback::OnStatsDelivered(
    const webrtc::scoped_refptr<const webrtc::RTCStatsReport>& report) {
  std::move(result_callback_)(report);
}

RTCStatsCallback::RTCStatsCallback(ResultCallback result_callback)
    : result_callback_(std::move(result_callback)) {}

}  // namespace sora
