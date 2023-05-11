// Based: https://source.chromium.org/chromium/_/webrtc/src.git/+/f88487c5c94e19fa984ce52965598c24ac3706c7:modules/audio_coding/codecs/opus/audio_encoder_opus.cc
#include "sora/audio_encoder_lyra.h"

#include <cstdlib>

// Boost
#include <boost/dll/runtime_symbol_info.hpp>

// WebRTC
#include <absl/strings/match.h>
#include <api/audio_codecs/audio_encoder.h>
#include <common_audio/smoothing_filter.h>
#include <modules/audio_coding/audio_network_adaptor/audio_network_adaptor_impl.h>
#include <modules/audio_coding/audio_network_adaptor/controller_manager.h>
#include <rtc_base/logging.h>

// Lyra
#include <lyra.h>

struct lyra_encoder;

namespace webrtc {

constexpr int kRtpTimestampRateHz = 16000;

absl::optional<std::string> GetFormatParameter(const SdpAudioFormat& format,
                                               absl::string_view param);

class AudioEncoderLyraImpl final : public AudioEncoder {
 public:
  using AudioNetworkAdaptorCreator =
      std::function<std::unique_ptr<AudioNetworkAdaptor>(absl::string_view,
                                                         RtcEventLog*)>;

  AudioEncoderLyraImpl(const sora::AudioEncoderLyraConfig& config,
                       int payload_type);

  // Dependency injection for testing.
  AudioEncoderLyraImpl(
      const sora::AudioEncoderLyraConfig& config,
      int payload_type,
      const AudioNetworkAdaptorCreator& audio_network_adaptor_creator,
      std::unique_ptr<SmoothingFilter> bitrate_smoother);

  AudioEncoderLyraImpl(int payload_type, const SdpAudioFormat& format);
  ~AudioEncoderLyraImpl() override;

  AudioEncoderLyraImpl(const AudioEncoderLyraImpl&) = delete;
  AudioEncoderLyraImpl& operator=(const AudioEncoderLyraImpl&) = delete;

  int SampleRateHz() const override;
  size_t NumChannels() const override;
  int RtpTimestampRateHz() const override;
  size_t Num10MsFramesInNextPacket() const override;
  size_t Max10MsFramesInAPacket() const override;
  int GetTargetBitrate() const override;

  void Reset() override;
  bool SetFec(bool enable) override;

  bool SetDtx(bool enable) override;
  bool GetDtx() const override;

  bool SetApplication(Application application) override;
  void SetMaxPlaybackRate(int frequency_hz) override;
  bool EnableAudioNetworkAdaptor(const std::string& config_string,
                                 RtcEventLog* event_log) override;
  void DisableAudioNetworkAdaptor() override;
  void OnReceivedUplinkPacketLossFraction(
      float uplink_packet_loss_fraction) override;
  void OnReceivedTargetAudioBitrate(int target_audio_bitrate_bps) override;
  void OnReceivedUplinkBandwidth(
      int target_audio_bitrate_bps,
      absl::optional<int64_t> bwe_period_ms) override;
  void OnReceivedUplinkAllocation(BitrateAllocationUpdate update) override;
  void OnReceivedRtt(int rtt_ms) override;
  void OnReceivedOverhead(size_t overhead_bytes_per_packet) override;
  void SetReceiverFrameLengthRange(int min_frame_length_ms,
                                   int max_frame_length_ms) override;
  ANAStats GetANAStats() const override;
  absl::optional<std::pair<TimeDelta, TimeDelta>> GetFrameLengthRange()
      const override;
  rtc::ArrayView<const int> supported_frame_lengths_ms() const {
    return config_.supported_frame_lengths_ms;
  }

 protected:
  EncodedInfo EncodeImpl(uint32_t rtp_timestamp,
                         rtc::ArrayView<const int16_t> audio,
                         rtc::Buffer* encoded) override;

 private:
  static absl::optional<sora::AudioEncoderLyraConfig> SdpToConfig(
      const SdpAudioFormat& format);
  static void AppendSupportedEncoders(std::vector<AudioCodecSpec>* specs);
  static AudioCodecInfo QueryAudioEncoder(
      const sora::AudioEncoderLyraConfig& config);
  static std::unique_ptr<AudioEncoder> MakeAudioEncoder(
      const sora::AudioEncoderLyraConfig&,
      int payload_type);

  size_t Num10msFramesPerPacket() const;
  size_t SamplesPer10msFrame() const;
  size_t SufficientOutputBufferSize() const;
  bool RecreateEncoderInstance(const sora::AudioEncoderLyraConfig& config);

  void OnReceivedUplinkBandwidth(
      int target_audio_bitrate_bps,
      absl::optional<int64_t> bwe_period_ms,
      absl::optional<int64_t> link_capacity_allocation);

  // TODO(minyue): remove "override" when we can deprecate
  // `AudioEncoder::SetTargetBitrate`.
  void SetTargetBitrate(int target_bps) override;

  void ApplyAudioNetworkAdaptor();
  std::unique_ptr<AudioNetworkAdaptor> DefaultAudioNetworkAdaptorCreator(
      absl::string_view config_string,
      RtcEventLog* event_log) const;

  sora::AudioEncoderLyraConfig config_;
  const int payload_type_;
  std::vector<int16_t> input_buffer_;
  lyra_encoder* inst_;
  uint32_t first_timestamp_in_buffer_;
  size_t num_channels_to_encode_;
  int next_frame_length_ms_;
  const AudioNetworkAdaptorCreator audio_network_adaptor_creator_;
  std::unique_ptr<AudioNetworkAdaptor> audio_network_adaptor_;
  int consecutive_dtx_frames_;

  friend struct sora::AudioEncoderLyra;
};

void AudioEncoderLyraImpl::AppendSupportedEncoders(
    std::vector<AudioCodecSpec>* specs) {
  auto path = boost::dll::program_location().parent_path() / "model_coeffs";
  std::string dir = path.string();
  auto env = std::getenv("SORA_LYRA_MODEL_COEFFS_PATH");
  if (env != NULL) {
    dir = env;
  }
  auto encoder = lyra_encoder_create(
      48000, 1, sora::AudioEncoderLyraConfig::kMinBitrateBps, false,
      dir.c_str());
  if (encoder == nullptr) {
    RTC_LOG(LS_WARNING) << "Failed to Create Lyra encoder: model_path=" << dir;
    return;
  }
  lyra_encoder_destroy(encoder);

  const SdpAudioFormat fmt = {"lyra", kRtpTimestampRateHz, 1, {}};
  const AudioCodecInfo info = QueryAudioEncoder(*SdpToConfig(fmt));
  specs->push_back({fmt, info});
}

AudioCodecInfo AudioEncoderLyraImpl::QueryAudioEncoder(
    const sora::AudioEncoderLyraConfig& config) {
  RTC_DCHECK(config.IsOk());
  AudioCodecInfo info(config.sample_rate_hz, config.num_channels,
                      config.bitrate_bps,
                      sora::AudioEncoderLyraConfig::kMinBitrateBps,
                      sora::AudioEncoderLyraConfig::kMaxBitrateBps);
  info.allow_comfort_noise = false;
  info.supports_network_adaption = true;
  return info;
}

std::unique_ptr<AudioEncoder> AudioEncoderLyraImpl::MakeAudioEncoder(
    const sora::AudioEncoderLyraConfig& config,
    int payload_type) {
  if (!config.IsOk()) {
    RTC_DCHECK_NOTREACHED();
    return nullptr;
  }
  return std::make_unique<AudioEncoderLyraImpl>(config, payload_type);
}

absl::optional<sora::AudioEncoderLyraConfig> AudioEncoderLyraImpl::SdpToConfig(
    const SdpAudioFormat& format) {
  if (!absl::EqualsIgnoreCase(format.name, "lyra")) {
    return absl::nullopt;
  }

  sora::AudioEncoderLyraConfig config;
  config.num_channels = format.num_channels;
  config.frame_size_ms = 20;
  config.dtx_enabled = GetFormatParameter(format, "usedtx") == "1";
  auto bitrate = GetFormatParameter(format, "bitrate");
  if (bitrate) {
    config.bitrate_bps = *rtc::StringToNumber<int>(*bitrate);
  }

  if (!config.IsOk()) {
    RTC_DCHECK_NOTREACHED();
    return absl::nullopt;
  }
  return config;
}

AudioEncoderLyraImpl::AudioEncoderLyraImpl(
    const sora::AudioEncoderLyraConfig& config,
    int payload_type)
    : AudioEncoderLyraImpl(
          config,
          payload_type,
          [this](absl::string_view config_string, RtcEventLog* event_log) {
            return DefaultAudioNetworkAdaptorCreator(config_string, event_log);
          },
          // We choose 5sec as initial time constant due to empirical data.
          std::make_unique<SmoothingFilterImpl>(5000)) {}

AudioEncoderLyraImpl::AudioEncoderLyraImpl(
    const sora::AudioEncoderLyraConfig& config,
    int payload_type,
    const AudioNetworkAdaptorCreator& audio_network_adaptor_creator,
    std::unique_ptr<SmoothingFilter> bitrate_smoother)
    : payload_type_(payload_type),
      inst_(nullptr),
      audio_network_adaptor_creator_(audio_network_adaptor_creator),
      consecutive_dtx_frames_(0) {
  RTC_DCHECK(0 <= payload_type && payload_type <= 127);
  RTC_CHECK(RecreateEncoderInstance(config));
}
AudioEncoderLyraImpl::~AudioEncoderLyraImpl() {
  if (inst_)
    lyra_encoder_destroy(inst_);
}

int AudioEncoderLyraImpl::SampleRateHz() const {
  return config_.sample_rate_hz;
}

size_t AudioEncoderLyraImpl::NumChannels() const {
  return config_.num_channels;
}

int AudioEncoderLyraImpl::RtpTimestampRateHz() const {
  return kRtpTimestampRateHz;
}

size_t AudioEncoderLyraImpl::Num10MsFramesInNextPacket() const {
  return Num10msFramesPerPacket();
}

size_t AudioEncoderLyraImpl::Max10MsFramesInAPacket() const {
  return Num10msFramesPerPacket();
}

int AudioEncoderLyraImpl::GetTargetBitrate() const {
  return config_.bitrate_bps;
}

void AudioEncoderLyraImpl::Reset() {
  RTC_CHECK(RecreateEncoderInstance(config_));
}

bool AudioEncoderLyraImpl::SetFec(bool enable) {
  return true;
}

bool AudioEncoderLyraImpl::SetDtx(bool enable) {
  config_.dtx_enabled = enable;
  return true;
}

bool AudioEncoderLyraImpl::GetDtx() const {
  return config_.dtx_enabled;
}

bool AudioEncoderLyraImpl::SetApplication(Application application) {
  return true;
}

void AudioEncoderLyraImpl::SetMaxPlaybackRate(int frequency_hz) {}

bool AudioEncoderLyraImpl::EnableAudioNetworkAdaptor(
    const std::string& config_string,
    RtcEventLog* event_log) {
  audio_network_adaptor_ =
      audio_network_adaptor_creator_(config_string, event_log);
  return audio_network_adaptor_.get() != nullptr;
}

void AudioEncoderLyraImpl::DisableAudioNetworkAdaptor() {
  audio_network_adaptor_.reset(nullptr);
}

void AudioEncoderLyraImpl::OnReceivedUplinkPacketLossFraction(
    float uplink_packet_loss_fraction) {
  if (audio_network_adaptor_) {
    audio_network_adaptor_->SetUplinkPacketLossFraction(
        uplink_packet_loss_fraction);
    ApplyAudioNetworkAdaptor();
  }
}

void AudioEncoderLyraImpl::OnReceivedTargetAudioBitrate(
    int target_audio_bitrate_bps) {}

void AudioEncoderLyraImpl::OnReceivedUplinkBandwidth(
    int target_audio_bitrate_bps,
    absl::optional<int64_t> bwe_period_ms,
    absl::optional<int64_t> stable_target_bitrate_bps) {
  if (audio_network_adaptor_) {
    audio_network_adaptor_->SetTargetAudioBitrate(target_audio_bitrate_bps);
    if (stable_target_bitrate_bps)
      audio_network_adaptor_->SetUplinkBandwidth(*stable_target_bitrate_bps);

    ApplyAudioNetworkAdaptor();
  }
}
void AudioEncoderLyraImpl::OnReceivedUplinkBandwidth(
    int target_audio_bitrate_bps,
    absl::optional<int64_t> bwe_period_ms) {
  OnReceivedUplinkBandwidth(target_audio_bitrate_bps, bwe_period_ms,
                            absl::nullopt);
}

void AudioEncoderLyraImpl::OnReceivedUplinkAllocation(
    BitrateAllocationUpdate update) {
  OnReceivedUplinkBandwidth(update.target_bitrate.bps(), update.bwe_period.ms(),
                            update.stable_target_bitrate.bps());
}

void AudioEncoderLyraImpl::OnReceivedRtt(int rtt_ms) {
  if (!audio_network_adaptor_)
    return;
  audio_network_adaptor_->SetRtt(rtt_ms);
  ApplyAudioNetworkAdaptor();
}

void AudioEncoderLyraImpl::OnReceivedOverhead(
    size_t overhead_bytes_per_packet) {
  if (audio_network_adaptor_) {
    audio_network_adaptor_->SetOverhead(overhead_bytes_per_packet);
    ApplyAudioNetworkAdaptor();
  }
}

void AudioEncoderLyraImpl::SetReceiverFrameLengthRange(
    int min_frame_length_ms,
    int max_frame_length_ms) {}

AudioEncoder::EncodedInfo AudioEncoderLyraImpl::EncodeImpl(
    uint32_t rtp_timestamp,
    rtc::ArrayView<const int16_t> audio,
    rtc::Buffer* encoded) {
  if (input_buffer_.empty())
    first_timestamp_in_buffer_ = rtp_timestamp;

  input_buffer_.insert(input_buffer_.end(), audio.cbegin(), audio.cend());
  if (input_buffer_.size() <
      (Num10msFramesPerPacket() * SamplesPer10msFrame())) {
    return EncodedInfo();
  }
  RTC_CHECK_EQ(input_buffer_.size(),
               Num10msFramesPerPacket() * SamplesPer10msFrame());

  const size_t max_encoded_bytes = 23;
  EncodedInfo info;
  info.encoded_bytes = encoded->AppendData(
      max_encoded_bytes, [&](rtc::ArrayView<uint8_t> encoded) {
        auto v = lyra_encoder_encode(
            inst_, &input_buffer_[0],
            rtc::CheckedDivExact(input_buffer_.size(), config_.num_channels));
        auto size = lyra_vector_u8_get_size(v);
        auto p = lyra_vector_u8_get_data(v);
        std::memcpy(encoded.data(), p, size);
        lyra_vector_u8_destroy(v);
        return size;
      });
  input_buffer_.clear();

  bool dtx_frame = (info.encoded_bytes == 0);

  // Will use new packet size for next encoding.
  config_.frame_size_ms = next_frame_length_ms_;

  info.encoded_timestamp = first_timestamp_in_buffer_;
  info.payload_type = payload_type_;
  info.send_even_if_empty = true;  // Allows Opus to send empty packets.
  // After 20 DTX frames (MAX_CONSECUTIVE_DTX) Opus will send a frame
  // coding the background noise. Avoid flagging this frame as speech
  // (even though there is a probability of the frame being speech).
  info.speech = !dtx_frame && (consecutive_dtx_frames_ != 20);
  info.encoder_type = CodecType::kOther;

  // Increase or reset DTX counter.
  consecutive_dtx_frames_ = dtx_frame ? (consecutive_dtx_frames_ + 1) : 0;

  return info;
}

size_t AudioEncoderLyraImpl::Num10msFramesPerPacket() const {
  return static_cast<size_t>(rtc::CheckedDivExact(config_.frame_size_ms, 10));
}

size_t AudioEncoderLyraImpl::SamplesPer10msFrame() const {
  return rtc::CheckedDivExact(config_.sample_rate_hz, 100) *
         config_.num_channels;
}

size_t AudioEncoderLyraImpl::SufficientOutputBufferSize() const {
  // Calculate the number of bytes we expect the encoder to produce,
  // then multiply by two to give a wide margin for error.
  const size_t bytes_per_millisecond =
      static_cast<size_t>(config_.bitrate_bps / (1000 * 8) + 1);
  const size_t approx_encoded_bytes =
      Num10msFramesPerPacket() * 10 * bytes_per_millisecond;
  return 2 * approx_encoded_bytes;
}

// If the given config is OK, recreate the Lyra encoder instance with those
// settings, save the config, and return true. Otherwise, do nothing and return
// false.
bool AudioEncoderLyraImpl::RecreateEncoderInstance(
    const sora::AudioEncoderLyraConfig& config) {
  if (!config.IsOk())
    return false;
  config_ = config;
  if (inst_)
    lyra_encoder_destroy(inst_);
  input_buffer_.clear();
  input_buffer_.reserve(Num10msFramesPerPacket() * SamplesPer10msFrame());
  const int bitrate = config.bitrate_bps;
  auto path = boost::dll::program_location().parent_path() / "model_coeffs";
  std::string dir = path.string();
  auto env = std::getenv("SORA_LYRA_MODEL_COEFFS_PATH");
  if (env != NULL) {
    dir = env;
  }
  inst_ = lyra_encoder_create(config.sample_rate_hz, config.num_channels,
                              bitrate, config.dtx_enabled, dir.c_str());
  RTC_LOG(LS_INFO) << "Created Lyra encoder: sample_rate_hz="
                   << config.sample_rate_hz
                   << " num_channels=" << config.num_channels
                   << " bitrate=" << bitrate
                   << " dtx_enabled=" << config.dtx_enabled;
  num_channels_to_encode_ = config.num_channels;
  next_frame_length_ms_ = config_.frame_size_ms;
  return true;
}

void AudioEncoderLyraImpl::SetTargetBitrate(int bits_per_second) {}

void AudioEncoderLyraImpl::ApplyAudioNetworkAdaptor() {
  auto config = audio_network_adaptor_->GetEncoderRuntimeConfig();

  if (config.enable_dtx)
    SetDtx(*config.enable_dtx);
}

std::unique_ptr<AudioNetworkAdaptor>
AudioEncoderLyraImpl::DefaultAudioNetworkAdaptorCreator(
    absl::string_view config_string,
    RtcEventLog* event_log) const {
  AudioNetworkAdaptorImpl::Config config;
  config.event_log = event_log;
  return std::unique_ptr<AudioNetworkAdaptor>(new AudioNetworkAdaptorImpl(
      config,
      ControllerManagerImpl::Create(
          (std::string)config_string, NumChannels(), supported_frame_lengths_ms(),
          sora::AudioEncoderLyraConfig::kMinBitrateBps, num_channels_to_encode_,
          next_frame_length_ms_, GetTargetBitrate(), false, GetDtx())));
}

ANAStats AudioEncoderLyraImpl::GetANAStats() const {
  if (audio_network_adaptor_) {
    return audio_network_adaptor_->GetStats();
  }
  return ANAStats();
}

absl::optional<std::pair<TimeDelta, TimeDelta>>
AudioEncoderLyraImpl::GetFrameLengthRange() const {
  if (config_.supported_frame_lengths_ms.empty()) {
    return absl::nullopt;
  } else if (audio_network_adaptor_) {
    return {{TimeDelta::Millis(config_.supported_frame_lengths_ms.front()),
             TimeDelta::Millis(config_.supported_frame_lengths_ms.back())}};
  } else {
    return {{TimeDelta::Millis(config_.frame_size_ms),
             TimeDelta::Millis(config_.frame_size_ms)}};
  }
}

}  // namespace webrtc

namespace sora {

AudioEncoderLyraConfig::AudioEncoderLyraConfig()
    : frame_size_ms(kDefaultFrameSizeMs),
      sample_rate_hz(16000),
      num_channels(1),
      bitrate_bps(kMinBitrateBps),
      dtx_enabled(false) {
  supported_frame_lengths_ms.push_back(20);
}

bool AudioEncoderLyraConfig::IsOk() const {
  RTC_LOG(LS_INFO) << "AudioEncoderLyraConfig: frame_size_ms=" << frame_size_ms
                   << " sample_rate_hz=" << sample_rate_hz
                   << " num_channels=" << num_channels
                   << " bitrate_bps=" << bitrate_bps
                   << " dtx_enabled=" << dtx_enabled;
  if (frame_size_ms != 20)
    return false;
  if (sample_rate_hz != 16000 && sample_rate_hz != 48000) {
    // Unsupported input sample rate. (libopus supports a few other rates as
    // well; we can add support for them when needed.)
    return false;
  }
  // monoral only
  if (num_channels != 1) {
    return false;
  }
  if (bitrate_bps < kMinBitrateBps || bitrate_bps > kMaxBitrateBps)
    return false;
  return true;
}

absl::optional<AudioEncoderLyraConfig> AudioEncoderLyra::SdpToConfig(
    const webrtc::SdpAudioFormat& audio_format) {
  return webrtc::AudioEncoderLyraImpl::SdpToConfig(audio_format);
}
void AudioEncoderLyra::AppendSupportedEncoders(
    std::vector<webrtc::AudioCodecSpec>* specs) {
  webrtc::AudioEncoderLyraImpl::AppendSupportedEncoders(specs);
}
webrtc::AudioCodecInfo AudioEncoderLyra::QueryAudioEncoder(
    const AudioEncoderLyraConfig& config) {
  return webrtc::AudioEncoderLyraImpl::QueryAudioEncoder(config);
}
std::unique_ptr<webrtc::AudioEncoder> AudioEncoderLyra::MakeAudioEncoder(
    const AudioEncoderLyraConfig& config,
    int payload_type,
    absl::optional<webrtc::AudioCodecPairId> codec_pair_id) {
  if (!config.IsOk()) {
    RTC_DCHECK_NOTREACHED();
    return nullptr;
  }
  return webrtc::AudioEncoderLyraImpl::MakeAudioEncoder(config, payload_type);
}

}  // namespace sora
