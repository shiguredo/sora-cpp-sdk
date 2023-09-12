#ifndef SORA_MAC_AUDIO_OUTPUT_HELPER_H_
#define SORA_MAC_AUDIO_OUTPUT_HELPER_H_

// WebRTC
#include <base/RTCMacros.h>

#include "sora/audio_output_helper.h"

RTC_FWD_DECL_OBJC_CLASS(SoraRTCAudioSessionDelegateAdapter);

namespace sora {

class MacAudioOutputHelper : public AudioOutputHelperInterface {
 public:
  MacAudioOutputHelper(AudioChangeRouteObserver* observer);
  ~MacAudioOutputHelper();

  bool IsHandsfree() override;
  void SetHandsfree(bool enable) override;

 private:
  SoraRTCAudioSessionDelegateAdapter* adapter_;
};

}  // namespace sora

#endif  // SORA_MAC_AUDIO_OUTPUT_HELPER_H_
