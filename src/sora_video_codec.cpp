#include "sora/sora_video_codec.h"

// WebRTC
#include <api/video_codecs/builtin_video_decoder_factory.h>
#include <api/video_codecs/builtin_video_encoder_factory.h>

#include "sora/open_h264_video_encoder.h"

#if defined(SORA_CPP_SDK_IOS) || defined(SORA_CPP_SDK_MACOS)
#include "sora/mac/mac_video_factory.h"
#elif defined(SORA_CPP_SDK_ANDROID)
#include "sora/android/android_video_factory.h"
#endif

#if defined(USE_VPL_ENCODER)
#include "sora/hwenc_vpl/vpl_video_codec.h"
#endif

#if defined(USE_NVCODEC_ENCODER)
#include "sora/hwenc_nvcodec/nvcodec_video_codec.h"
#endif

namespace webrtc {

// VideoCodecType
void tag_invoke(const boost::json::value_from_tag&,
                boost::json::value& jv,
                const VideoCodecType& v) {
  jv = CodecTypeToPayloadString(v);
}
VideoCodecType tag_invoke(const boost::json::value_to_tag<VideoCodecType>&,
                          boost::json::value const& jv) {
  return PayloadStringToCodecType(jv.as_string().c_str());
}

}  // namespace webrtc

namespace sora {

// VideoCodecImplementation
void tag_invoke(const boost::json::value_from_tag&,
                boost::json::value& jv,
                const VideoCodecImplementation& v) {
  switch (v) {
    case VideoCodecImplementation::kInternal:
      jv = "internal";
      break;
    case VideoCodecImplementation::kCiscoOpenH264:
      jv = "cisco_openh264";
      break;
    case VideoCodecImplementation::kIntelVpl:
      jv = "intel_vpl";
      break;
    case VideoCodecImplementation::kNvidiaVideoCodecSdk:
      jv = "nvidia_video_codec_sdk";
      break;
  }
}
VideoCodecImplementation tag_invoke(
    const boost::json::value_to_tag<VideoCodecImplementation>&,
    boost::json::value const& jv) {
  if (jv.is_string()) {
    std::string s = jv.as_string().c_str();
    if (s == "internal") {
      return VideoCodecImplementation::kInternal;
    }
    if (s == "cisco_openh264") {
      return VideoCodecImplementation::kCiscoOpenH264;
    }
    if (s == "intel_vpl") {
      return VideoCodecImplementation::kIntelVpl;
    }
    if (s == "nvidia_video_codec_sdk") {
      return VideoCodecImplementation::kNvidiaVideoCodecSdk;
    }
  }
  throw std::invalid_argument("invalid VideoCodecImplementation");
}

// VideoCodecCapability::Parameters
void tag_invoke(const boost::json::value_from_tag&,
                boost::json::value& jv,
                const VideoCodecCapability::Parameters& v) {
  auto& jo = jv.emplace_object();
  if (v.version) {
    jo["version"] = *v.version;
  }
  if (v.openh264_path) {
    jo["openh264_path"] = *v.openh264_path;
  }
}

VideoCodecCapability::Parameters tag_invoke(
    const boost::json::value_to_tag<VideoCodecCapability::Parameters>&,
    boost::json::value const& jv) {
  VideoCodecCapability::Parameters r;
  if (jv.is_object()) {
    if (jv.at("version").is_string()) {
      r.version = jv.at("version").as_string().c_str();
    }
    if (jv.at("openh264_path").is_string()) {
      r.openh264_path = jv.at("openh264_path").as_string().c_str();
    }
  }
  return r;
}
// VideoCodecCapability::Codec
void tag_invoke(const boost::json::value_from_tag&,
                boost::json::value& jv,
                const VideoCodecCapability::Codec& v) {
  auto& jo = jv.emplace_object();
  jo["type"] = boost::json::value_from(v.type);
  jo["decoder"] = v.decoder;
  jo["encoder"] = v.encoder;
  jo["parameters"] = boost::json::value_from(v.parameters);
}
VideoCodecCapability::Codec tag_invoke(
    const boost::json::value_to_tag<VideoCodecCapability::Codec>&,
    boost::json::value const& jv) {
  VideoCodecCapability::Codec r(webrtc::kVideoCodecGeneric, false, false);
  if (!jv.is_object()) {
    throw std::invalid_argument("invalid VideoCodecCapability::Codec");
  }
  if (jv.at("type").is_string()) {
    r.type = boost::json::value_to<webrtc::VideoCodecType>(jv.at("type"));
  }
  if (jv.at("decoder").is_bool()) {
    r.decoder = jv.at("decoder").as_bool();
  }
  if (jv.at("encoder").is_bool()) {
    r.encoder = jv.at("encoder").as_bool();
  }
  if (jv.at("params").is_object()) {
    r.parameters = boost::json::value_to<VideoCodecCapability::Parameters>(
        jv.at("parameters"));
  }
  return r;
}
// VideoCodecCapability::Engine
void tag_invoke(const boost::json::value_from_tag&,
                boost::json::value& jv,
                const VideoCodecCapability::Engine& v) {
  auto& jo = jv.emplace_object();
  jo["name"] = boost::json::value_from(v.name);
  auto& ja = jo["codecs"].emplace_array();
  for (const auto& codec : v.codecs) {
    ja.push_back(boost::json::value_from(codec));
  }
  jo["parameters"] = boost::json::value_from(v.parameters);
}
VideoCodecCapability::Engine tag_invoke(
    const boost::json::value_to_tag<VideoCodecCapability::Engine>&,
    boost::json::value const& jv) {
  VideoCodecCapability::Engine r(VideoCodecImplementation::kInternal);
  if (!jv.is_object()) {
    throw std::invalid_argument("invalid VideoCodecCapability::Engine");
  }
  if (jv.at("name").is_string()) {
    r.name = boost::json::value_to<VideoCodecImplementation>(jv.at("name"));
  }
  if (jv.at("codecs").is_array()) {
    for (const auto& codec : jv.at("codecs").as_array()) {
      r.codecs.push_back(
          boost::json::value_to<VideoCodecCapability::Codec>(codec));
    }
  }
  if (jv.at("parameters").is_object()) {
    r.parameters = boost::json::value_to<VideoCodecCapability::Parameters>(
        jv.at("parameters"));
  }
  return r;
}
// VideoCodecCapability
void tag_invoke(const boost::json::value_from_tag&,
                boost::json::value& jv,
                const VideoCodecCapability& v) {
  auto& jo = jv.emplace_object();
  auto& ja = jo["engines"].emplace_array();
  for (const auto& engine : v.engines) {
    ja.push_back(boost::json::value_from(engine));
  }
}
VideoCodecCapability tag_invoke(
    const boost::json::value_to_tag<VideoCodecCapability>&,
    boost::json::value const& jv) {
  VideoCodecCapability r;
  if (!jv.is_object()) {
    throw std::invalid_argument("invalid VideoCodecCapability");
  }
  if (!jv.at("engines").is_array()) {
    throw std::invalid_argument("invalid VideoCodecCapability");
  }
  for (const auto& engine : jv.at("engines").as_array()) {
    r.engines.push_back(
        boost::json::value_to<VideoCodecCapability::Engine>(engine));
  }
  return r;
}

VideoCodecCapability GetVideoCodecCapability(
    VideoCodecCapabilityConfig config) {
  VideoCodecCapability cap;
  // kInternal
  {
    auto& engine =
        cap.engines.emplace_back(VideoCodecImplementation::kInternal);
#if defined(SORA_CPP_SDK_IOS) || defined(SORA_CPP_SDK_MACOS)
    auto internal_encoder_factory = CreateMacVideoEncoderFactory();
    auto internal_decoder_factory = CreateMacVideoDecoderFactory();
#elif defined(SORA_CPP_SDK_ANDROID)
    auto internal_encoder_factory =
        config.jni_env == nullptr ? nullptr
                                  : CreateAndroidVideoEncoderFactory(
                                        static_cast<JNIEnv*>(config.jni_env));
    auto internal_decoder_factory =
        config.jni_env == nullptr ? nullptr
                                  : CreateAndroidVideoDecoderFactory(
                                        static_cast<JNIEnv*>(config.jni_env));
#else
    auto internal_encoder_factory = webrtc::CreateBuiltinVideoEncoderFactory();
    auto internal_decoder_factory = webrtc::CreateBuiltinVideoDecoderFactory();
#endif
    if (internal_encoder_factory && internal_decoder_factory) {
      auto encoder_formats = internal_encoder_factory->GetSupportedFormats();
      auto decoder_formats = internal_decoder_factory->GetSupportedFormats();
      auto has_format = [](const std::vector<webrtc::SdpVideoFormat>& formats,
                           webrtc::VideoCodecType type) {
        for (const auto& format : formats) {
          if (webrtc::PayloadStringToCodecType(format.name) == type) {
            return true;
          }
        }
        return false;
      };
      engine.codecs.emplace_back(
          webrtc::kVideoCodecVP8,
          has_format(encoder_formats, webrtc::kVideoCodecVP8),
          has_format(decoder_formats, webrtc::kVideoCodecVP8));
      engine.codecs.emplace_back(
          webrtc::kVideoCodecVP9,
          has_format(encoder_formats, webrtc::kVideoCodecVP9),
          has_format(decoder_formats, webrtc::kVideoCodecVP9));
      engine.codecs.emplace_back(
          webrtc::kVideoCodecH264,
          has_format(encoder_formats, webrtc::kVideoCodecH264),
          has_format(decoder_formats, webrtc::kVideoCodecH264));
      engine.codecs.emplace_back(
          webrtc::kVideoCodecH265,
          has_format(encoder_formats, webrtc::kVideoCodecH265),
          has_format(decoder_formats, webrtc::kVideoCodecH265));
      engine.codecs.emplace_back(
          webrtc::kVideoCodecAV1,
          has_format(encoder_formats, webrtc::kVideoCodecAV1),
          has_format(decoder_formats, webrtc::kVideoCodecAV1));
    }
  }

  // kCiscoOpenH264
  cap.engines.push_back(GetOpenH264VideoCodecCapability(config.openh264_path));

#if defined(USE_VPL_ENCODER)
  cap.engines.push_back(GetVplVideoCodecCapability(config.vpl_session));
#endif

#if defined(USE_NVCODEC_ENCODER)
  cap.engines.push_back(GetNvCodecVideoCodecCapability(config.cuda_context));
#endif

  // 全て false のエンジンを削除
  cap.engines.erase(
      std::remove_if(cap.engines.begin(), cap.engines.end(),
                     [](const VideoCodecCapability::Engine& engine) {
                       return std::all_of(
                           engine.codecs.begin(), engine.codecs.end(),
                           [](const VideoCodecCapability::Codec& codec) {
                             return !codec.decoder && !codec.encoder;
                           });
                     }),
      cap.engines.end());
  return cap;
}

}  // namespace sora
