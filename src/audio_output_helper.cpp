#include "sora/audio_output_helper.h"

// WebRTC
#include <rtc_base/logging.h>

#if defined(__APPLE__)
#include "sora/mac/mac_audio_output_helper.h"
#endif

namespace sora {

AudioOutputHelper::AudioOutputHelper(AudioChangeRouteObserver* observer) {
#if defined(__APPLE__)
  impl_.reset(new MacAudioOutputHelper(observer));
#endif
}

bool AudioOutputHelper::IsHandsfree() {
  if (!impl_) {
    RTC_LOG(LS_ERROR) << "AudioOutputHelper not availabe in this platform.";
    return false;
  }
  return impl_->IsHandsfree();
}

void AudioOutputHelper::SetHandsfree(bool enable) {
  if (!impl_) {
    RTC_LOG(LS_ERROR) << "AudioOutputHelper not availabe in this platform.";
    return;
  }
  impl_->SetHandsfree(enable);
}

}  // namespace sora