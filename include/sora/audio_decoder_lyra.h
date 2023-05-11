#ifndef SORA_AUDIO_DECODER_LYRA_H_
#define SORA_AUDIO_DECODER_LYRA_H_

// WebRTC
#include <absl/types/optional.h>
#include <api/audio_codecs/audio_codec_pair_id.h>
#include <api/audio_codecs/audio_decoder.h>
#include <api/audio_codecs/audio_format.h>

namespace sora {

struct AudioDecoderLyra {
  struct Config {
    bool IsOk() const { return true; }
    int sample_rate_hz = 16000;
    int num_channels = 1;
  };
  static absl::optional<Config> SdpToConfig(
      const webrtc::SdpAudioFormat& audio_format);
  static void AppendSupportedDecoders(
      std::vector<webrtc::AudioCodecSpec>* specs);

  static std::unique_ptr<webrtc::AudioDecoder> MakeAudioDecoder(
      Config config,
      absl::optional<webrtc::AudioCodecPairId> codec_pair_id = absl::nullopt);
};

}  // namespace sora

#endif
