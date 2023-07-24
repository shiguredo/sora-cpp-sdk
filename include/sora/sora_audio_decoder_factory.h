#ifndef SORA_SORA_AUDIO_DECODER_FACTORY_H_
#define SORA_SORA_AUDIO_DECODER_FACTORY_H_

#include <api/audio_codecs/audio_decoder_factory.h>

namespace sora {

rtc::scoped_refptr<webrtc::AudioDecoderFactory>
CreateBuiltinAudioDecoderFactory();

}

#endif
