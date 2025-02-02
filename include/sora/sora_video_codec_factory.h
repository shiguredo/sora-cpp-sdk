#ifndef SORA_SORA_VIDEO_CODEC_FACTORY_H_
#define SORA_SORA_VIDEO_CODEC_FACTORY_H_

#include <memory>

#include "sora/sora_video_codec.h"
#include "sora/sora_video_decoder_factory.h"
#include "sora/sora_video_encoder_factory.h"

namespace sora {

struct SoraVideoCodecFactoryConfig {
  // この preference に従って利用するエンコーダ/デコーダの実装を選択する
  //
  // std::nullopt の場合は VideoCodecImplementation::kInternal なエンコーダ/デコーダを利用する。
  std::optional<VideoCodecPreference> preference;

  // VideoCodecCapability を生成するために必要なエンコーダ/デコーダのコンテキスト
  //
  // 例えば Intel VPL を利用したい場合、capability_config.vpl_session に値を設定する必要がある
  VideoCodecCapabilityConfig capability_config;

  // エンコーダ/デコーダファクトリの設定
  //
  // encoder_factory_config.encoders と decoder_factory_config.decoders は
  // CreateVideoCodecFactory 内でクリアされるので、これらは設定しなくても良い。
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