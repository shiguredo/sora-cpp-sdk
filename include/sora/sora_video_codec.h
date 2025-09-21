#ifndef SORA_SORA_VIDEO_CODEC_H_
#define SORA_SORA_VIDEO_CODEC_H_

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

// WebRTC
#include <api/video/video_codec_type.h>

#include "amf_context.h"
#include "boost_json_iwyu.h"
#include "cuda_context.h"

namespace webrtc {

// VideoCodecType
void tag_invoke(const boost::json::value_from_tag&,
                boost::json::value& jv,
                const VideoCodecType& v);
VideoCodecType tag_invoke(const boost::json::value_to_tag<VideoCodecType>&,
                          boost::json::value const& jv);

}  // namespace webrtc

namespace sora {

enum class VideoCodecImplementation {
  kInternal,
  kCiscoOpenH264,
  kIntelVpl,
  kNvidiaVideoCodec,
  kAmdAmf,
  kRaspiV4L2M2M,
  // 連番になってて微妙だが、ユーザー側でカスタムエンコーダ/デコーダを実現しつつ、現在の
  // capability, preference の機能に乗せるならこのようにするのが一番分かりやすい
  kCustom_1 = 100,
  kCustom_2,
  kCustom_3,
  kCustom_4,
  kCustom_5,
  kCustom_6,
  kCustom_7,
  kCustom_8,
  kCustom_9,
};

bool IsCustomImplementation(VideoCodecImplementation implementation);

struct VideoCodecCapability {
  struct Parameters {
    std::optional<std::string> version;
    std::optional<std::string> openh264_path;
    std::optional<std::string> vpl_impl;
    std::optional<int> vpl_impl_value;
    std::optional<std::string> nvcodec_gpu_device_name;
    std::optional<std::string> amf_runtime_version;
    std::optional<std::string> amf_embedded_version;
    std::optional<std::string> custom_engine_name;
    std::optional<std::string> custom_engine_description;
  };
  struct Codec {
    Codec(webrtc::VideoCodecType type, bool encoder, bool decoder)
        : type(type), encoder(encoder), decoder(decoder) {}
    webrtc::VideoCodecType type;
    bool encoder;
    bool decoder;
    Parameters parameters;
  };
  struct Engine {
    Engine(VideoCodecImplementation name) : name(name) {}
    VideoCodecImplementation name;
    std::vector<Codec> codecs;
    Parameters parameters;
  };
  std::vector<Engine> engines;
};

// VideoCodecImplementation
void tag_invoke(const boost::json::value_from_tag&,
                boost::json::value& jv,
                const VideoCodecImplementation& v);
VideoCodecImplementation tag_invoke(
    const boost::json::value_to_tag<VideoCodecImplementation>&,
    boost::json::value const& jv);
// VideoCodecCapability::Parameters
void tag_invoke(const boost::json::value_from_tag&,
                boost::json::value& jv,
                const VideoCodecCapability::Parameters& v);
VideoCodecCapability::Parameters tag_invoke(
    const boost::json::value_to_tag<VideoCodecCapability::Parameters>&,
    boost::json::value const& jv);
// VideoCodecCapability::Codec
void tag_invoke(const boost::json::value_from_tag&,
                boost::json::value& jv,
                const VideoCodecCapability::Codec& v);
VideoCodecCapability::Codec tag_invoke(
    const boost::json::value_to_tag<VideoCodecCapability::Codec>&,
    boost::json::value const& jv);
// VideoCodecCapability::Engine
void tag_invoke(const boost::json::value_from_tag&,
                boost::json::value& jv,
                const VideoCodecCapability::Engine& v);
VideoCodecCapability::Engine tag_invoke(
    const boost::json::value_to_tag<VideoCodecCapability::Engine>&,
    boost::json::value const& jv);
// VideoCodecCapability
void tag_invoke(const boost::json::value_from_tag&,
                boost::json::value& jv,
                const VideoCodecCapability& v);
VideoCodecCapability tag_invoke(
    const boost::json::value_to_tag<VideoCodecCapability>&,
    boost::json::value const& jv);

struct VideoCodecCapabilityConfig {
  VideoCodecCapabilityConfig();
  std::shared_ptr<CudaContext> cuda_context;
  std::shared_ptr<AMFContext> amf_context;
  std::optional<std::string> openh264_path;
  void* jni_env = nullptr;
  std::function<std::vector<VideoCodecCapability::Engine>()> get_custom_engines;
};

// 利用可能なエンコーダ/デコーダ実装の一覧を取得する
VideoCodecCapability GetVideoCodecCapability(VideoCodecCapabilityConfig config);

struct VideoCodecPreference {
  struct Parameters {};
  struct Codec {
    Codec() : type(webrtc::kVideoCodecGeneric) {}
    explicit Codec(
        webrtc::VideoCodecType type,
        std::optional<VideoCodecImplementation> encoder = std::nullopt,
        std::optional<VideoCodecImplementation> decoder = std::nullopt,
        Parameters parameters = Parameters())
        : type(type),
          encoder(encoder),
          decoder(decoder),
          parameters(parameters) {}
    webrtc::VideoCodecType type;
    std::optional<VideoCodecImplementation> encoder;
    std::optional<VideoCodecImplementation> decoder;
    Parameters parameters;
  };
  VideoCodecPreference() = default;
  explicit VideoCodecPreference(std::vector<Codec> codecs) : codecs(codecs) {}
  std::vector<Codec> codecs;
  Codec* Find(webrtc::VideoCodecType type);
  const Codec* Find(webrtc::VideoCodecType type) const;
  Codec& GetOrAdd(webrtc::VideoCodecType type);
  bool HasImplementation(VideoCodecImplementation implementation) const;

  void Merge(const VideoCodecPreference& preference);
};

// VideoCodecPreference::Parameters
void tag_invoke(const boost::json::value_from_tag&,
                boost::json::value& jv,
                const VideoCodecPreference::Parameters& v);
VideoCodecPreference::Parameters tag_invoke(
    const boost::json::value_to_tag<VideoCodecPreference::Parameters>&,
    boost::json::value const& jv);
// VideoCodecPreference::Codec
void tag_invoke(const boost::json::value_from_tag&,
                boost::json::value& jv,
                const VideoCodecPreference::Codec& v);
VideoCodecPreference::Codec tag_invoke(
    const boost::json::value_to_tag<VideoCodecPreference::Codec>&,
    boost::json::value const& jv);
// VideoCodecPreference
void tag_invoke(const boost::json::value_from_tag&,
                boost::json::value& jv,
                const VideoCodecPreference& v);
VideoCodecPreference tag_invoke(
    const boost::json::value_to_tag<VideoCodecPreference>&,
    boost::json::value const& jv);

// 指定された preference が capability に対して妥当かどうかを検証する
bool ValidateVideoCodecPreference(const VideoCodecPreference& preference,
                                  const VideoCodecCapability& capability,
                                  std::vector<std::string>* errors);

// implementation で指定されたコーデックのみの preference を作る
VideoCodecPreference CreateVideoCodecPreferenceFromImplementation(
    const VideoCodecCapability& capability,
    VideoCodecImplementation implementation);

}  // namespace sora

#endif