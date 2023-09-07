/*
 *  Copyright 2018 The WebRTC Project Authors. All rights reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */
 
 #include "sora/mac/mac_audio_output_helper.h"

// WebRTC
#include <rtc_base/logging.h>

// WebRTC
#import "sdk/objc/components/audio/RTCAudioSession.h"
#import "sdk/objc/helpers/RTCDispatcher.h"

NS_ASSUME_NONNULL_BEGIN

@interface SoraRTCAudioSessionDelegateAdapter : NSObject <RTC_OBJC_TYPE (RTCAudioSessionDelegate)>

- (instancetype)init NS_UNAVAILABLE;

- (instancetype)initWithObserver:(sora::AudioChangeRouteObserver *)observer NS_DESIGNATED_INITIALIZER;

@end

NS_ASSUME_NONNULL_END

@implementation SoraRTCAudioSessionDelegateAdapter {
  sora::AudioChangeRouteObserver *_observer;
}

- (instancetype)initWithObserver:(sora::AudioChangeRouteObserver *)observer {
  RTC_DCHECK(observer);
  if (self = [super init]) {
    _observer = observer;
  }
  return self;
}

- (void)audioSessionDidBeginInterruption:(RTC_OBJC_TYPE(RTCAudioSession) *)session {
}

- (void)audioSessionDidEndInterruption:(RTC_OBJC_TYPE(RTCAudioSession) *)session
                   shouldResumeSession:(BOOL)shouldResumeSession {
}

- (void)audioSessionDidChangeRoute:(RTC_OBJC_TYPE(RTCAudioSession) *)session
                            reason:(AVAudioSessionRouteChangeReason)reason
                     previousRoute:(AVAudioSessionRouteDescription *)previousRoute {
  switch (reason) {
    case AVAudioSessionRouteChangeReasonUnknown:
    case AVAudioSessionRouteChangeReasonNewDeviceAvailable:
    case AVAudioSessionRouteChangeReasonOldDeviceUnavailable:
    case AVAudioSessionRouteChangeReasonCategoryChange:
      // It turns out that we see a category change (at least in iOS 9.2)
      // when making a switch from a BT device to e.g. Speaker using the
      // iOS Control Center and that we therefore must check if the sample
      // rate has changed. And if so is the case, restart the audio unit.
    case AVAudioSessionRouteChangeReasonOverride:
    case AVAudioSessionRouteChangeReasonWakeFromSleep:
    case AVAudioSessionRouteChangeReasonNoSuitableRouteForCategory:
      _observer->OnDidChangeRoute();
      break;
    case AVAudioSessionRouteChangeReasonRouteConfigurationChange:
      // The set of input and output ports has not changed, but their
      // configuration has, e.g., a port’s selected data source has
      // changed. Ignore this type of route change since we are focusing
      // on detecting headset changes.
      // RTCLog(@"Ignoring RouteConfigurationChange");
      break;
  }
}

- (void)audioSessionMediaServerTerminated:(RTC_OBJC_TYPE(RTCAudioSession) *)session {
}

- (void)audioSessionMediaServerReset:(RTC_OBJC_TYPE(RTCAudioSession) *)session {
}

- (void)audioSession:(RTC_OBJC_TYPE(RTCAudioSession) *)session
    didChangeCanPlayOrRecord:(BOOL)canPlayOrRecord {
}

- (void)audioSessionDidStartPlayOrRecord:(RTC_OBJC_TYPE(RTCAudioSession) *)session {
}

- (void)audioSessionDidStopPlayOrRecord:(RTC_OBJC_TYPE(RTCAudioSession) *)session {
}

- (void)audioSession:(RTC_OBJC_TYPE(RTCAudioSession) *)audioSession
    didChangeOutputVolume:(float)outputVolume {
}

@end

namespace sora {

MacAudioOutputHelper::MacAudioOutputHelper(AudioChangeRouteObserver* observer) {
  adapter_ = [[SoraRTCAudioSessionDelegateAdapter alloc] initWithObserver:observer];

  RTC_OBJC_TYPE(RTCAudioSession)* session = [RTC_OBJC_TYPE(RTCAudioSession) sharedInstance];
  [session addDelegate:adapter_];
}

MacAudioOutputHelper::~MacAudioOutputHelper() {
  // あった方が良いと思ったが、 libwebrtc のサンプルにないので消しておく
  // RTC_OBJC_TYPE(RTCAudioSession)* session = [RTC_OBJC_TYPE(RTCAudioSession) sharedInstance];
  // [session removeDelegate:adapter_];
  adapter_ = nil;
}

bool MacAudioOutputHelper::IsHandsfree() {
  RTC_OBJC_TYPE(RTCAudioSession)* session = [RTC_OBJC_TYPE(RTCAudioSession) sharedInstance];
  AVAudioSessionPortDescription *output = session.currentRoute.outputs.firstObject;
  AVAudioSessionPortDescription *input = session.currentRoute.inputs.firstObject;
  return [output.portType isEqualToString:AVAudioSessionPortBuiltInSpeaker] &&
         [input.portType isEqualToString:AVAudioSessionPortBuiltInMic];
}

void MacAudioOutputHelper::SetHandsfree(bool enable) {
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
                                                  RTC_LOG(LS_ERROR) << "Failed to set Handsfree " << enable;
                                                }
                                                [session unlockForConfiguration];
                                              }];
}

}