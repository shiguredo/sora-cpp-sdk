//
//  AudioOutputHelperWrapper.mm
//  hello
//
//  Created by tnoho on 2023/09/07.
//

#import "AudioOutputHelperWrapper.h"

#include "sora/audio_output_helper.h"

class AudioOutputHelperWrapperImpl : public sora::AudioOutputHelper, public sora::AudioChangeRouteObserver {
private:
    __weak id<AudioChangeRouteDelegate> delegate_;
public:
    AudioOutputHelperWrapperImpl(id<AudioChangeRouteDelegate> delegate) : sora::AudioOutputHelper(this), delegate_(delegate) {}
    
    void OnDidChangeRoute() override {
        if (delegate_) {
            [delegate_ didChangeRoute];
        }
    }
};

@implementation AudioOutputHelperWrapper {
    AudioOutputHelperWrapperImpl* audioHelper_;
}

- (instancetype)initWithDelegate:(id<AudioChangeRouteDelegate>)delegate {
    self = [super init];
    if (self) {
        audioHelper_ = new AudioOutputHelperWrapperImpl(delegate);
    }
    return self;
}

- (void)dealloc {
    delete audioHelper_;
}

- (BOOL)isHandsfree {
    return audioHelper_->IsHandsfree();
}

- (void)setHandsfree:(BOOL)enable {
    audioHelper_->SetHandsfree(enable);
}

@end
