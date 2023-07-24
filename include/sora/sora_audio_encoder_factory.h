#ifndef SORA_SORA_AUDIO_ENCODER_FACTORY_H_
#define SORA_SORA_AUDIO_ENCODER_FACTORY_H_

#include <api/audio_codecs/audio_encoder_factory.h>

namespace sora {

rtc::scoped_refptr<webrtc::AudioEncoderFactory>
CreateBuiltinAudioEncoderFactory();

}

#endif
