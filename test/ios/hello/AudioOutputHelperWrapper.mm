//
//  AudioOutputHelperWrapper.mm
//  hello
//
//  Created by tnoho on 2023/09/07.
//

#import "AudioOutputHelperWrapper.h"

#include "sora/audio_output_helper.h"

#include <memory>

class AudioChangeRouteObserverWrapper : public sora::AudioChangeRouteObserver {
private:
    __weak id<AudioChangeRouteDelegate> delegate_;
public:
    AudioChangeRouteObserverWrapper(id<AudioChangeRouteDelegate> delegate) : delegate_(delegate) {
    }
    
    void OnChangeRoute() override {
        if (delegate_) {
            [delegate_ didChangeRoute];
        }
    }
};

@implementation AudioOutputHelperWrapper {
    std::unique_ptr<sora::AudioOutputHelperInterface> audio_output_helper_;
    AudioChangeRouteObserverWrapper* observer_;
}

- (instancetype)initWithDelegate:(id<AudioChangeRouteDelegate>)delegate {
    self = [super init];
    if (self) {
        observer_ = new AudioChangeRouteObserverWrapper(delegate);
        audio_output_helper_ = CreateAudioOutputHelper(observer_);
    }
    return self;
}

- (void)dealloc {
    audio_output_helper_.reset();
    delete observer_;
}

- (BOOL)isHandsfree {
    return audio_output_helper_->IsHandsfree();
}

- (void)setHandsfree:(BOOL)enable {
    audio_output_helper_->SetHandsfree(enable);
}

@end
