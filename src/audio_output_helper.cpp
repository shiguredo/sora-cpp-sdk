#include "sora/audio_output_helper.h"

#include <memory>

// WebRTC
#include <rtc_base/logging.h>

#if defined(SORA_CPP_SDK_IOS) || defined(SORA_CPP_SDK_VISIONOS)
#include "sora/mac/mac_audio_output_helper.h"
#endif

namespace sora {

class DummyAudioOutputHelper : public AudioOutputHelperInterface {
 public:
  bool IsHandsfree() override {
    RTC_LOG(LS_ERROR) << "AudioOutputHelper not availabe in this platform.";
    return false;
  }
  void SetHandsfree(bool enable) override {
    RTC_LOG(LS_ERROR) << "AudioOutputHelper not availabe in this platform.";
    return;
  }
};

std::unique_ptr<AudioOutputHelperInterface> CreateAudioOutputHelper(
    AudioChangeRouteObserver* observer) {
#if defined(SORA_CPP_SDK_IOS) || defined(SORA_CPP_SDK_VISIONOS)
  return std::make_unique<MacAudioOutputHelper>(observer);
#else
  return std::make_unique<DummyAudioOutputHelper>();
#endif
}

}  // namespace sora