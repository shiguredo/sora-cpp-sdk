#include "sora/sora_audio_encoder_factory.h"

#include <memory>
#include <vector>

#include <api/audio_codecs/L16/audio_encoder_L16.h>
#include <api/audio_codecs/audio_encoder_factory_template.h>
#include <api/audio_codecs/g711/audio_encoder_g711.h>
#include <api/audio_codecs/g722/audio_encoder_g722.h>
#include <api/audio_codecs/opus/audio_encoder_multi_channel_opus.h>
#include <api/audio_codecs/opus/audio_encoder_opus.h>

#if USE_LYRA
#include "sora/audio_encoder_lyra.h"
#endif

namespace sora {

namespace {

// Modify an audio encoder to not advertise support for anything.
template <typename T>
struct NotAdvertised {
  using Config = typename T::Config;
  static absl::optional<Config> SdpToConfig(
      const webrtc::SdpAudioFormat& audio_format) {
    return T::SdpToConfig(audio_format);
  }
  static void AppendSupportedEncoders(
      std::vector<webrtc::AudioCodecSpec>* specs) {
    // Don't advertise support for anything.
  }
  static webrtc::AudioCodecInfo QueryAudioEncoder(const Config& config) {
    return T::QueryAudioEncoder(config);
  }
  static std::unique_ptr<webrtc::AudioEncoder> MakeAudioEncoder(
      const Config& config,
      int payload_type,
      absl::optional<webrtc::AudioCodecPairId> codec_pair_id = absl::nullopt,
      const webrtc::FieldTrialsView* field_trials = nullptr) {
    return T::MakeAudioEncoder(config, payload_type, codec_pair_id,
                               field_trials);
  }
};

}  // namespace

rtc::scoped_refptr<webrtc::AudioEncoderFactory>
CreateBuiltinAudioEncoderFactory() {
  return webrtc::CreateAudioEncoderFactory<
      webrtc::AudioEncoderOpus,
      NotAdvertised<webrtc::AudioEncoderMultiChannelOpus>,
      webrtc::AudioEncoderG722, webrtc::AudioEncoderG711,
      NotAdvertised<webrtc::AudioEncoderL16>
#if USE_LYRA
      ,
      sora::AudioEncoderLyra
#endif
      >();
}

}  // namespace sora