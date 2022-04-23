#include "sora/rtc_stats.h"

namespace sora {

rtc::scoped_refptr<RTCStatsCallback> RTCStatsCallback::Create(
    ResultCallback result_callback) {
  return rtc::make_ref_counted<RTCStatsCallback>(std::move(result_callback));
}

void RTCStatsCallback::OnStatsDelivered(
    const rtc::scoped_refptr<const webrtc::RTCStatsReport>& report) {
  std::move(result_callback_)(report);
}

RTCStatsCallback::RTCStatsCallback(ResultCallback result_callback)
    : result_callback_(std::move(result_callback)) {}

}  // namespace sora
