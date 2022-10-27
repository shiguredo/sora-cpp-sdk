#include "sora/audio_decoder_lyra.h"

#include <cstdlib>

// Boost
#include <boost/dll/runtime_symbol_info.hpp>

// WebRTC
#include <absl/strings/match.h>
#include <rtc_base/logging.h>

#include "sora/dyn/lyra.h"

struct lyra_decoder;

namespace webrtc {

class LyraFrame : public AudioDecoder::EncodedAudioFrame {
 public:
  LyraFrame(AudioDecoder* decoder,
            rtc::Buffer&& payload,
            bool is_primary_payload)
      : decoder_(decoder),
        payload_(std::move(payload)),
        is_primary_payload_(is_primary_payload) {}

  size_t Duration() const override {
    int ret;
    if (is_primary_payload_) {
      ret = decoder_->PacketDuration(payload_.data(), payload_.size());
    } else {
      ret = decoder_->PacketDurationRedundant(payload_.data(), payload_.size());
    }
    return (ret < 0) ? 0 : static_cast<size_t>(ret);
  }

  bool IsDtxPacket() const override { return payload_.size() == 0; }

  absl::optional<DecodeResult> Decode(
      rtc::ArrayView<int16_t> decoded) const override {
    AudioDecoder::SpeechType speech_type = AudioDecoder::kSpeech;
    int ret;
    if (is_primary_payload_) {
      ret = decoder_->Decode(
          payload_.data(), payload_.size(), decoder_->SampleRateHz(),
          decoded.size() * sizeof(int16_t), decoded.data(), &speech_type);
    } else {
      ret = decoder_->DecodeRedundant(
          payload_.data(), payload_.size(), decoder_->SampleRateHz(),
          decoded.size() * sizeof(int16_t), decoded.data(), &speech_type);
    }

    if (ret < 0)
      return absl::nullopt;

    return DecodeResult{static_cast<size_t>(ret), speech_type};
  }

 private:
  AudioDecoder* const decoder_;
  const rtc::Buffer payload_;
  const bool is_primary_payload_;
};

class AudioDecoderLyraImpl final : public AudioDecoder {
 public:
  explicit AudioDecoderLyraImpl(size_t num_channels,
                                int sample_rate_hz = 16000);
  ~AudioDecoderLyraImpl() override;

  AudioDecoderLyraImpl(const AudioDecoderLyraImpl&) = delete;
  AudioDecoderLyraImpl& operator=(const AudioDecoderLyraImpl&) = delete;

  std::vector<ParseResult> ParsePayload(rtc::Buffer&& payload,
                                        uint32_t timestamp) override;
  void Reset() override;
  int PacketDuration(const uint8_t* encoded, size_t encoded_len) const override;
  int PacketDurationRedundant(const uint8_t* encoded,
                              size_t encoded_len) const override;
  bool PacketHasFec(const uint8_t* encoded, size_t encoded_len) const override;
  int SampleRateHz() const override;
  size_t Channels() const override;

 protected:
  int DecodeInternal(const uint8_t* encoded,
                     size_t encoded_len,
                     int sample_rate_hz,
                     int16_t* decoded,
                     SpeechType* speech_type) override;
  int DecodeRedundantInternal(const uint8_t* encoded,
                              size_t encoded_len,
                              int sample_rate_hz,
                              int16_t* decoded,
                              SpeechType* speech_type) override;

 private:
  lyra_decoder* dec_state_;
  const size_t channels_;
  const int sample_rate_hz_;
};

AudioDecoderLyraImpl::AudioDecoderLyraImpl(size_t num_channels,
                                           int sample_rate_hz)
    : channels_{num_channels},
      sample_rate_hz_{sample_rate_hz},
      dec_state_(nullptr) {
  RTC_DCHECK(num_channels == 1);
  RTC_DCHECK(sample_rate_hz == 16000 || sample_rate_hz == 48000);
  Reset();
}

AudioDecoderLyraImpl::~AudioDecoderLyraImpl() {
  dyn::lyra_decoder_destroy(dec_state_);
}

std::vector<AudioDecoder::ParseResult> AudioDecoderLyraImpl::ParsePayload(
    rtc::Buffer&& payload,
    uint32_t timestamp) {
  std::vector<ParseResult> results;

  std::unique_ptr<EncodedAudioFrame> frame(
      new LyraFrame(this, std::move(payload), true));
  results.emplace_back(timestamp, 0, std::move(frame));
  return results;
}

int AudioDecoderLyraImpl::DecodeInternal(const uint8_t* encoded,
                                         size_t encoded_len,
                                         int sample_rate_hz,
                                         int16_t* decoded,
                                         SpeechType* speech_type) {
  RTC_DCHECK_EQ(sample_rate_hz, sample_rate_hz_);
  auto r =
      dyn::lyra_decoder_set_encoded_packet(dec_state_, encoded, encoded_len);
  if (!r) {
    return -1;
  }
  auto v = dyn::lyra_decoder_decode_samples(dec_state_, sample_rate_hz_ / 50);
  if (v == nullptr) {
    return -1;
  }
  auto samples = dyn::lyra_vector_s16_get_size(v);
  auto p = dyn::lyra_vector_s16_get_data(v);
  std::memcpy(decoded, p, samples * 2);
  dyn::lyra_vector_s16_destroy(v);
  return samples;
}

int AudioDecoderLyraImpl::DecodeRedundantInternal(const uint8_t* encoded,
                                                  size_t encoded_len,
                                                  int sample_rate_hz,
                                                  int16_t* decoded,
                                                  SpeechType* speech_type) {
  return DecodeInternal(encoded, encoded_len, sample_rate_hz, decoded,
                        speech_type);
}

void AudioDecoderLyraImpl::Reset() {
  if (dec_state_)
    dyn::lyra_decoder_destroy(dec_state_);
  auto path = boost::dll::program_location().parent_path() / "model_coeffs";
  std::string dir = path.string();
  auto env = std::getenv("SORA_LYRA_MODEL_COEFFS_PATH");
  if (env != NULL) {
    dir = env;
  }
  dec_state_ =
      dyn::lyra_decoder_create(sample_rate_hz_, channels_, dir.c_str());
}

int AudioDecoderLyraImpl::PacketDuration(const uint8_t* encoded,
                                         size_t encoded_len) const {
  return sample_rate_hz_ / 50;
}

int AudioDecoderLyraImpl::PacketDurationRedundant(const uint8_t* encoded,
                                                  size_t encoded_len) const {
  return PacketDuration(encoded, encoded_len);
}

bool AudioDecoderLyraImpl::PacketHasFec(const uint8_t* encoded,
                                        size_t encoded_len) const {
  return false;
}

int AudioDecoderLyraImpl::SampleRateHz() const {
  return sample_rate_hz_;
}

size_t AudioDecoderLyraImpl::Channels() const {
  return channels_;
}

}  // namespace webrtc

namespace sora {

absl::optional<AudioDecoderLyra::Config> AudioDecoderLyra::SdpToConfig(
    const webrtc::SdpAudioFormat& audio_format) {
  if (!absl::EqualsIgnoreCase(audio_format.name, "lyra")) {
    return absl::nullopt;
  }
  return Config();
}
void AudioDecoderLyra::AppendSupportedDecoders(
    std::vector<webrtc::AudioCodecSpec>* specs) {
  if (!dyn::DynModule::IsLoadable(dyn::LYRA_SO)) {
    RTC_LOG(LS_WARNING) << "Lyra is not supported";
    return;
  }
  webrtc::AudioCodecInfo lyra_info{16000, 1, 3200, 3200, 9200};
  lyra_info.allow_comfort_noise = false;
  lyra_info.supports_network_adaption = false;
  webrtc::SdpAudioFormat lyra_format({"lyra", 16000, 1, {}});
  specs->push_back({std::move(lyra_format), lyra_info});
}

std::unique_ptr<webrtc::AudioDecoder> AudioDecoderLyra::MakeAudioDecoder(
    Config config,
    absl::optional<webrtc::AudioCodecPairId> codec_pair_id,
    const webrtc::FieldTrialsView* field_trials) {
  return std::make_unique<webrtc::AudioDecoderLyraImpl>(config.num_channels,
                                                        config.sample_rate_hz);
}

}  // namespace sora
