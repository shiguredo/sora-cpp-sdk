#ifndef SORA_AUDIO_ENCODER_LYRA_H_
#define SORA_AUDIO_ENCODER_LYRA_H_

// WebRTC
#include <absl/types/optional.h>
#include <api/audio_codecs/audio_codec_pair_id.h>
#include <api/audio_codecs/audio_encoder.h>
#include <api/audio_codecs/audio_format.h>

namespace sora {

struct AudioEncoderLyraConfig {
  static constexpr int kDefaultFrameSizeMs = 20;

  static constexpr int kMinBitrateBps = 3200;
  static constexpr int kMaxBitrateBps = 9200;

  AudioEncoderLyraConfig();

  bool IsOk() const;

  int frame_size_ms;
  int sample_rate_hz;
  size_t num_channels;

  int bitrate_bps = 3200;

  bool dtx_enabled;
  std::vector<int> supported_frame_lengths_ms;
};

struct AudioEncoderLyra {
  using Config = AudioEncoderLyraConfig;
  static absl::optional<AudioEncoderLyraConfig> SdpToConfig(
      const webrtc::SdpAudioFormat& audio_format);
  static void AppendSupportedEncoders(
      std::vector<webrtc::AudioCodecSpec>* specs);
  static webrtc::AudioCodecInfo QueryAudioEncoder(
      const AudioEncoderLyraConfig& config);
  static std::unique_ptr<webrtc::AudioEncoder> MakeAudioEncoder(
      const AudioEncoderLyraConfig& config,
      int payload_type,
      absl::optional<webrtc::AudioCodecPairId> codec_pair_id = absl::nullopt);
};

}  // namespace sora

#endif
