#include "sora/mac/mac_audio_output_manager.h"

// WebRTC
#include <rtc_base/logging.h>

// WebRTC
#import "sdk/objc/components/audio/RTCAudioSession.h"
#import "sdk/objc/helpers/RTCDispatcher.h"


namespace sora {

void MacAudioOutputManager::UseSpeaker(bool enable) {
  AVAudioSessionPortOverride portOverride = AVAudioSessionPortOverrideNone;
  if (enable) {
    portOverride = AVAudioSessionPortOverrideSpeaker;
  }
  [RTC_OBJC_TYPE(RTCDispatcher) dispatchAsyncOnType:RTCDispatcherTypeAudioSession
                                              block:^{
                                                RTC_OBJC_TYPE(RTCAudioSession) *session =
                                                    [RTC_OBJC_TYPE(RTCAudioSession) sharedInstance];
                                                [session lockForConfiguration];
                                                NSError *error = nil;
                                                if (![session overrideOutputAudioPort:portOverride
                                                                               error:&error]) {
                                                  RTC_LOG(LS_ERROR) << "Failed to change speaker  enable=" << enable;
                                                }
                                                [session unlockForConfiguration];
                                              }];
}

}