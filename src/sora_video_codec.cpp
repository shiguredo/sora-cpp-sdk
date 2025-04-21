#include "sora/sora_video_codec.h"

// WebRTC
#include <api/video_codecs/builtin_video_decoder_factory.h>
#include <api/video_codecs/builtin_video_encoder_factory.h>
#include <rtc_base/logging.h>

#include "sora/java_context.h"
#include "sora/open_h264_video_codec.h"

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

#if defined(USE_AMF_ENCODER)
#include "sora/hwenc_amf/amf_video_codec.h"
#endif

#if defined(USE_XCODER_ENCODER)
#include "sora/hwenc_xcoder/xcoder_video_codec.h"
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
    case VideoCodecImplementation::kAmdAmf:
      jv = "amd_amf";
      break;
    case VideoCodecImplementation::kNetintLibxcoder:
      jv = "netint_libxcoder";
      break;
  }
}
VideoCodecImplementation tag_invoke(
    const boost::json::value_to_tag<VideoCodecImplementation>&,
    boost::json::value const& jv) {
  std::string s = jv.as_string().c_str();
  if (s == "internal") {
    return VideoCodecImplementation::kInternal;
  } else if (s == "cisco_openh264") {
    return VideoCodecImplementation::kCiscoOpenH264;
  } else if (s == "intel_vpl") {
    return VideoCodecImplementation::kIntelVpl;
  } else if (s == "nvidia_video_codec_sdk") {
    return VideoCodecImplementation::kNvidiaVideoCodecSdk;
  } else if (s == "amd_amf") {
    return VideoCodecImplementation::kAmdAmf;
  } else if (s == "netint_libxcoder") {
    return VideoCodecImplementation::kNetintLibxcoder;
  }
  throw std::invalid_argument("Invalid VideoCodecImplementation");
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
  if (v.vpl_impl) {
    jo["vpl_impl"] = *v.vpl_impl;
  }
  if (v.vpl_impl_value) {
    jo["vpl_impl_value"] = *v.vpl_impl_value;
  }
  if (v.nvcodec_gpu_device_name) {
    jo["nvcodec_gpu_device_name"] = *v.nvcodec_gpu_device_name;
  }
  if (v.amf_runtime_version) {
    jo["amf_runtime_version"] = *v.amf_runtime_version;
  }
  if (v.amf_embedded_version) {
    jo["amf_embedded_version"] = *v.amf_embedded_version;
  }
}

VideoCodecCapability::Parameters tag_invoke(
    const boost::json::value_to_tag<VideoCodecCapability::Parameters>&,
    boost::json::value const& jv) {
  VideoCodecCapability::Parameters r;
  const auto& jo = jv.as_object();
  if (jo.contains("version")) {
    r.version = jo.at("version").as_string().c_str();
  }
  if (jo.contains("openh264_path")) {
    r.openh264_path = jo.at("openh264_path").as_string().c_str();
  }
  if (jo.contains("vpl_impl")) {
    r.vpl_impl = jo.at("vpl_impl").as_string().c_str();
  }
  if (jo.contains("vpl_impl_value")) {
    r.vpl_impl_value = (int)jo.at("vpl_impl_value").as_int64();
  }
  if (jo.contains("nvcodec_gpu_device_name")) {
    r.nvcodec_gpu_device_name =
        jo.at("nvcodec_gpu_device_name").as_string().c_str();
  }
  if (jo.contains("amf_runtime_version")) {
    r.amf_runtime_version = jo.at("amf_runtime_version").as_string().c_str();
  }
  if (jo.contains("amf_embedded_version")) {
    r.amf_embedded_version = jo.at("amf_embedded_version").as_string().c_str();
  }
  return r;
}
// VideoCodecCapability::Codec
void tag_invoke(const boost::json::value_from_tag&,
                boost::json::value& jv,
                const VideoCodecCapability::Codec& v) {
  auto& jo = jv.emplace_object();
  jo["type"] = boost::json::value_from(v.type);
  jo["encoder"] = v.encoder;
  jo["decoder"] = v.decoder;
  jo["parameters"] = boost::json::value_from(v.parameters);
}
VideoCodecCapability::Codec tag_invoke(
    const boost::json::value_to_tag<VideoCodecCapability::Codec>&,
    boost::json::value const& jv) {
  VideoCodecCapability::Codec r(webrtc::kVideoCodecGeneric, false, false);
  r.type = boost::json::value_to<webrtc::VideoCodecType>(jv.at("type"));
  r.encoder = jv.at("encoder").as_bool();
  r.decoder = jv.at("decoder").as_bool();
  r.parameters = boost::json::value_to<VideoCodecCapability::Parameters>(
      jv.at("parameters"));
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
  r.name = boost::json::value_to<VideoCodecImplementation>(jv.at("name"));
  for (const auto& codec : jv.at("codecs").as_array()) {
    r.codecs.push_back(
        boost::json::value_to<VideoCodecCapability::Codec>(codec));
  }
  r.parameters = boost::json::value_to<VideoCodecCapability::Parameters>(
      jv.at("parameters"));
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
  for (const auto& engine : jv.at("engines").as_array()) {
    r.engines.push_back(
        boost::json::value_to<VideoCodecCapability::Engine>(engine));
  }
  return r;
}

VideoCodecCapabilityConfig::VideoCodecCapabilityConfig() {
#if defined(SORA_CPP_SDK_ANDROID)
  jni_env = GetJNIEnv();
#endif
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
  cap.engines.push_back(GetVplVideoCodecCapability(VplSession::Create()));
#endif

#if defined(USE_NVCODEC_ENCODER)
  cap.engines.push_back(GetNvCodecVideoCodecCapability(config.cuda_context));
#endif

#if defined(USE_AMF_ENCODER)
  cap.engines.push_back(GetAMFVideoCodecCapability(config.amf_context));
#endif

#if defined(USE_XCODER_ENCODER)
  cap.engines.push_back(GetLibxcoderVideoCodecCapability());
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

VideoCodecPreference::Codec* VideoCodecPreference::Find(
    webrtc::VideoCodecType type) {
  auto it = std::find_if(codecs.begin(), codecs.end(),
                         [type](const VideoCodecPreference::Codec& codec) {
                           return codec.type == type;
                         });
  if (it == codecs.end()) {
    return nullptr;
  }
  return &*it;
}
const VideoCodecPreference::Codec* VideoCodecPreference::Find(
    webrtc::VideoCodecType type) const {
  auto it = std::find_if(codecs.begin(), codecs.end(),
                         [type](const VideoCodecPreference::Codec& codec) {
                           return codec.type == type;
                         });
  if (it == codecs.end()) {
    return nullptr;
  }
  return &*it;
}
VideoCodecPreference::Codec& VideoCodecPreference::GetOrAdd(
    webrtc::VideoCodecType type) {
  if (auto* codec = Find(type); codec != nullptr) {
    return *codec;
  }
  codecs.push_back(Codec(type, std::nullopt, std::nullopt));
  return codecs.back();
}
bool VideoCodecPreference::HasImplementation(
    VideoCodecImplementation implementation) const {
  return std::any_of(
      codecs.begin(), codecs.end(), [implementation](const Codec& codec) {
        return (codec.encoder && *codec.encoder == implementation) ||
               (codec.decoder && *codec.decoder == implementation);
      });
}
void VideoCodecPreference::Merge(const VideoCodecPreference& preference) {
  for (const auto& codec : preference.codecs) {
    if (auto* c = Find(codec.type); c != nullptr) {
      if (codec.encoder) {
        c->encoder = codec.encoder;
      }
      if (codec.decoder) {
        c->decoder = codec.decoder;
      }
      if (codec.encoder || codec.decoder) {
        c->parameters = codec.parameters;
      }
    } else {
      codecs.push_back(codec);
    }
  }
}

// VideoCodecPreference::Parameters
void tag_invoke(const boost::json::value_from_tag&,
                boost::json::value& jv,
                const VideoCodecPreference::Parameters& v) {
  jv.emplace_object();
}
VideoCodecPreference::Parameters tag_invoke(
    const boost::json::value_to_tag<VideoCodecPreference::Parameters>&,
    boost::json::value const& jv) {
  VideoCodecPreference::Parameters r;
  return r;
}
// VideoCodecPreference::Codec
void tag_invoke(const boost::json::value_from_tag&,
                boost::json::value& jv,
                const VideoCodecPreference::Codec& v) {
  auto& jo = jv.emplace_object();
  jo["type"] = boost::json::value_from(v.type);
  jo["encoder"] =
      v.encoder ? boost::json::value_from(*v.encoder) : boost::json::value();
  jo["decoder"] =
      v.decoder ? boost::json::value_from(*v.decoder) : boost::json::value();
  jo["parameters"] = boost::json::value_from(v.parameters);
}
VideoCodecPreference::Codec tag_invoke(
    const boost::json::value_to_tag<VideoCodecPreference::Codec>&,
    boost::json::value const& jv) {
  VideoCodecPreference::Codec r;
  r.type = boost::json::value_to<webrtc::VideoCodecType>(jv.at("type"));
  if (jv.at("encoder").is_string()) {
    r.encoder =
        boost::json::value_to<VideoCodecImplementation>(jv.at("encoder"));
  }
  if (jv.at("decoder").is_string()) {
    r.decoder =
        boost::json::value_to<VideoCodecImplementation>(jv.at("decoder"));
  }
  r.parameters = boost::json::value_to<VideoCodecPreference::Parameters>(
      jv.at("parameters"));
  return r;
}
// VideoCodecPreference
void tag_invoke(const boost::json::value_from_tag&,
                boost::json::value& jv,
                const VideoCodecPreference& v) {
  auto& jo = jv.emplace_object();
  auto& ja = jo["codecs"].emplace_array();
  for (const auto& codec : v.codecs) {
    ja.push_back(boost::json::value_from(codec));
  }
}
VideoCodecPreference tag_invoke(
    const boost::json::value_to_tag<VideoCodecPreference>&,
    boost::json::value const& jv) {
  VideoCodecPreference r;
  for (const auto& codec : jv.at("codecs").as_array()) {
    r.codecs.push_back(
        boost::json::value_to<VideoCodecPreference::Codec>(codec));
  }
  return r;
}

bool ValidateVideoCodecPreference(const VideoCodecPreference& preference,
                                  const VideoCodecCapability& capability,
                                  std::vector<std::string>* errors) {
  if (errors == nullptr) {
    return false;
  }
  // preference に重複した type がないかチェック
  {
    auto get_count = [&preference](webrtc::VideoCodecType type) {
      return std::count_if(preference.codecs.begin(), preference.codecs.end(),
                           [type](const VideoCodecPreference::Codec& codec) {
                             return codec.type == type;
                           });
    };
    if (get_count(webrtc::kVideoCodecVP8) >= 2) {
      errors->push_back("duplicate VP8");
    }
    if (get_count(webrtc::kVideoCodecVP9) >= 2) {
      errors->push_back("duplicate VP9");
    }
    if (get_count(webrtc::kVideoCodecH264) >= 2) {
      errors->push_back("duplicate H264");
    }
    if (get_count(webrtc::kVideoCodecH265) >= 2) {
      errors->push_back("duplicate H265");
    }
    if (get_count(webrtc::kVideoCodecAV1) >= 2) {
      errors->push_back("duplicate AV1");
    }
  }
  // capability に存在しない implementation を設定してないか確認する
  {
    for (const auto& codec : preference.codecs) {
      // encoder
      if (codec.encoder) {
        auto engine =
            std::find_if(capability.engines.begin(), capability.engines.end(),
                         [&codec](const VideoCodecCapability::Engine& engine) {
                           return engine.name == *codec.encoder;
                         });
        if (engine == capability.engines.end()) {
          errors->push_back(
              "encoder implementation not found: codec_preference=" +
              boost::json::serialize(boost::json::value_from(codec)));
          continue;
        }
        auto codec_cap =
            std::find_if(engine->codecs.begin(), engine->codecs.end(),
                         [&codec](const VideoCodecCapability::Codec& c) {
                           return c.type == codec.type;
                         });
        if (codec_cap == engine->codecs.end()) {
          errors->push_back(
              "codec type not found: codec_preference=" +
              boost::json::serialize(boost::json::value_from(codec)));
          continue;
        }
        if (!codec_cap->encoder) {
          errors->push_back(
              "encoder not supported: codec_preference=" +
              boost::json::serialize(boost::json::value_from(codec)) +
              ", codec_capability=" +
              boost::json::serialize(boost::json::value_from(*codec_cap)));
          continue;
        }
      }
      // decoder
      if (codec.decoder) {
        auto engine =
            std::find_if(capability.engines.begin(), capability.engines.end(),
                         [&codec](const VideoCodecCapability::Engine& engine) {
                           return engine.name == *codec.decoder;
                         });
        if (engine == capability.engines.end()) {
          errors->push_back(
              "decoder implementation not found: codec_preference=" +
              boost::json::serialize(boost::json::value_from(codec)));
          continue;
        }
        auto codec_cap =
            std::find_if(engine->codecs.begin(), engine->codecs.end(),
                         [&codec](const VideoCodecCapability::Codec& c) {
                           return c.type == codec.type;
                         });
        if (codec_cap == engine->codecs.end()) {
          errors->push_back(
              "codec type not found: codec_preference=" +
              boost::json::serialize(boost::json::value_from(codec)));
          continue;
        }
        if (!codec_cap->decoder) {
          errors->push_back(
              "decoder not supported: codec_preference=" +
              boost::json::serialize(boost::json::value_from(codec)) +
              ", codec_capability=" +
              boost::json::serialize(boost::json::value_from(*codec_cap)));
          continue;
        }
      }
    }
  }

  if (!errors->empty()) {
    return false;
  }

  return true;
}

VideoCodecPreference CreateVideoCodecPreferenceFromImplementation(
    const VideoCodecCapability& capability,
    VideoCodecImplementation implementation) {
  VideoCodecPreference preference;
  auto specified_engine =
      std::find_if(capability.engines.begin(), capability.engines.end(),
                   [implementation](const auto& engine) {
                     return engine.name == implementation;
                   });
  if (specified_engine == capability.engines.end()) {
    return preference;
  }
  for (const auto& codec : specified_engine->codecs) {
    VideoCodecPreference::Codec preference_codec;
    preference_codec.type = codec.type;
    if (codec.encoder) {
      preference_codec.encoder = implementation;
    }
    if (codec.decoder) {
      preference_codec.decoder = implementation;
    }
    preference.codecs.push_back(preference_codec);
  }
  return preference;
}

}  // namespace sora
