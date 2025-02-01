#ifndef SORA_SORA_VIDEO_CODEC_FACTORY_H_
#define SORA_SORA_VIDEO_CODEC_FACTORY_H_

#include <memory>

#include "sora/sora_video_codec.h"
#include "sora/sora_video_decoder_factory.h"
#include "sora/sora_video_encoder_factory.h"

namespace sora {

struct SoraVideoCodecFactoryConfig {
  std::optional<VideoCodecPreference> preference;
  VideoCodecCapabilityConfig capability_config;
  SoraVideoEncoderFactoryConfig encoder_factory_config;
  SoraVideoDecoderFactoryConfig decoder_factory_config;
};

struct SoraVideoCodecFactory {
  std::unique_ptr<SoraVideoEncoderFactory> encoder_factory;
  std::unique_ptr<SoraVideoDecoderFactory> decoder_factory;
};

std::optional<SoraVideoCodecFactory> CreateVideoCodecFactory(
    const SoraVideoCodecFactoryConfig& config);

}  // namespace sora

#endif