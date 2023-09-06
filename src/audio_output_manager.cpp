#include "sora/audio_output_manager.h"

// WebRTC
#include <rtc_base/logging.h>

#if defined(__APPLE__)
#include "sora/mac/mac_audio_output_manager.h"
#endif

namespace sora {

void AudioOutputManager::UseSpeaker(bool enable) {
#if defined(__APPLE__)
  MacAudioOutputManager::UseSpeaker(enable);
  return;
#endif
  RTC_LOG(LS_ERROR) << "Failed to change speaker  enable=" << enable;
}

}  // namespace sora