#include "sora/sora_audio_decoder_factory.h"

#include <memory>
#include <vector>

#include <api/audio_codecs/L16/audio_decoder_L16.h>
#include <api/audio_codecs/audio_decoder_factory_template.h>
#include <api/audio_codecs/g711/audio_decoder_g711.h>
#include <api/audio_codecs/g722/audio_decoder_g722.h>
#include <api/audio_codecs/opus/audio_decoder_multi_channel_opus.h>
#include <api/audio_codecs/opus/audio_decoder_opus.h>

#include "sora/audio_decoder_lyra.h"

namespace sora {

namespace {

// Modify an audio decoder to not advertise support for anything.
template <typename T>
struct NotAdvertised {
  using Config = typename T::Config;
  static absl::optional<Config> SdpToConfig(
      const webrtc::SdpAudioFormat& audio_format) {
    return T::SdpToConfig(audio_format);
  }
  static void AppendSupportedDecoders(
      std::vector<webrtc::AudioCodecSpec>* specs) {
    // Don't advertise support for anything.
  }
  static std::unique_ptr<webrtc::AudioDecoder> MakeAudioDecoder(
      const Config& config,
      absl::optional<webrtc::AudioCodecPairId> codec_pair_id = absl::nullopt) {
    return T::MakeAudioDecoder(config, codec_pair_id);
  }
};

}  // namespace

rtc::scoped_refptr<webrtc::AudioDecoderFactory>
CreateBuiltinAudioDecoderFactory() {
  return webrtc::CreateAudioDecoderFactory<
      webrtc::AudioDecoderOpus,
      NotAdvertised<webrtc::AudioDecoderMultiChannelOpus>,
      webrtc::AudioDecoderG722, webrtc::AudioDecoderG711,
      NotAdvertised<webrtc::AudioDecoderL16>/*, sora::AudioDecoderLyra*/>();
}

}  // namespace sora
