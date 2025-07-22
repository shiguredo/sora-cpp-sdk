#include "sora/sora_video_codec_factory.h"

#include <algorithm>
#include <cassert>
#include <memory>
#include <optional>
#include <string>
#include <vector>

// Boost
#include <boost/json/serialize.hpp>
#include <boost/json/value_from.hpp>

// WebRTC
#include <api/video_codecs/builtin_video_decoder_factory.h>
#include <api/video_codecs/builtin_video_encoder_factory.h>
#include <api/video_codecs/sdp_video_format.h>
#include <api/video_codecs/video_codec.h>
#include <api/video_codecs/video_decoder_factory.h>
#include <api/video_codecs/video_encoder_factory.h>
#include <rtc_base/logging.h>

#include "sora/sora_video_decoder_factory.h"
#include "sora/sora_video_encoder_factory.h"
#include "sora/vpl_session.h"

#if !defined(__arm__) || defined(__aarch64__) || defined(__ARM_NEON__)
#endif

#if defined(__APPLE__)
#include "sora/mac/mac_video_factory.h"
#endif

#if defined(SORA_CPP_SDK_ANDROID)
#include "sora/android/android_video_factory.h"
#endif

#if defined(USE_NVCODEC_ENCODER)
#include "sora/hwenc_nvcodec/nvcodec_video_decoder.h"
#include "sora/hwenc_nvcodec/nvcodec_video_encoder.h"
#endif

#if defined(USE_VPL_ENCODER)
#include "sora/hwenc_vpl/vpl_video_decoder.h"
#include "sora/hwenc_vpl/vpl_video_encoder.h"
#endif

#if defined(USE_AMF_ENCODER)
#include "sora/hwenc_amf/amf_video_decoder.h"
#include "sora/hwenc_amf/amf_video_encoder.h"
#endif

#include "sora/open_h264_video_decoder.h"
#include "sora/open_h264_video_encoder.h"
#include "sora/sora_video_codec.h"

namespace sora {

std::optional<SoraVideoCodecFactory> CreateVideoCodecFactory(
    const SoraVideoCodecFactoryConfig& config) {
  auto capability = GetVideoCodecCapability(config.capability_config);
  auto preference = config.preference
                        ? *config.preference
                        : CreateVideoCodecPreferenceFromImplementation(
                              capability, VideoCodecImplementation::kInternal);
  RTC_LOG(LS_INFO) << "VideoCodecCapability: "
                   << boost::json::serialize(
                          boost::json::value_from(capability));
  RTC_LOG(LS_INFO) << "VideoCodecPreference: "
                   << boost::json::serialize(
                          boost::json::value_from(preference));

  std::vector<std::string> errors;
  if (!ValidateVideoCodecPreference(preference, capability, &errors)) {
    RTC_LOG(LS_ERROR) << "Failed to ValidateVideoCodecPreference:";
    for (const auto& error : errors) {
      RTC_LOG(LS_ERROR) << "  - " << error;
    }
    return std::nullopt;
  }

  SoraVideoEncoderFactoryConfig encoder_factory_config =
      config.encoder_factory_config;
  SoraVideoDecoderFactoryConfig decoder_factory_config =
      config.decoder_factory_config;
  encoder_factory_config.encoders.clear();
  decoder_factory_config.decoders.clear();

  std::shared_ptr<webrtc::VideoEncoderFactory> builtin_encoder_factory;
  std::shared_ptr<webrtc::VideoDecoderFactory> builtin_decoder_factory;
  // kInternal が設定されている場合は BuiltinVideoEncoderFactory/BuiltinVideoDecoderFactory を生成する
  if (std::find_if(preference.codecs.begin(), preference.codecs.end(),
                   [](const auto& codec) {
                     return codec.encoder &&
                            *codec.encoder ==
                                VideoCodecImplementation::kInternal;
                   }) != preference.codecs.end()) {
    builtin_encoder_factory = webrtc::CreateBuiltinVideoEncoderFactory();
#if defined(SORA_CPP_SDK_IOS) || defined(SORA_CPP_SDK_MACOS)
    builtin_encoder_factory = CreateMacVideoEncoderFactory();
#elif defined(SORA_CPP_SDK_ANDROID)
    builtin_encoder_factory =
        config.capability_config.jni_env == nullptr
            ? nullptr
            : CreateAndroidVideoEncoderFactory(
                  static_cast<JNIEnv*>(config.capability_config.jni_env));
#else
    builtin_encoder_factory = webrtc::CreateBuiltinVideoEncoderFactory();
#endif
  }
  if (std::find_if(preference.codecs.begin(), preference.codecs.end(),
                   [](const auto& codec) {
                     return codec.decoder &&
                            *codec.decoder ==
                                VideoCodecImplementation::kInternal;
                   }) != preference.codecs.end()) {
#if defined(SORA_CPP_SDK_IOS) || defined(SORA_CPP_SDK_MACOS)
    builtin_decoder_factory = CreateMacVideoDecoderFactory();
#elif defined(SORA_CPP_SDK_ANDROID)
    builtin_decoder_factory =
        config.capability_config.jni_env == nullptr
            ? nullptr
            : CreateAndroidVideoDecoderFactory(
                  static_cast<JNIEnv*>(config.capability_config.jni_env));
#else
    builtin_decoder_factory = webrtc::CreateBuiltinVideoDecoderFactory();
#endif
  }

  for (const auto& codec : preference.codecs) {
    if (codec.encoder) {
      if (*codec.encoder == VideoCodecImplementation::kInternal) {
        encoder_factory_config.encoders.push_back(
            VideoEncoderConfig(codec.type, builtin_encoder_factory));
      } else if (*codec.encoder == VideoCodecImplementation::kCiscoOpenH264) {
        assert(config.capability_config.openh264_path);
        auto create_video_encoder =
            [openh264_path = *config.capability_config.openh264_path](
                const webrtc::SdpVideoFormat& format) {
              return CreateOpenH264VideoEncoder(format, openh264_path);
            };
        encoder_factory_config.encoders.push_back(
            VideoEncoderConfig(codec.type, create_video_encoder, 16));
      } else if (*codec.encoder == VideoCodecImplementation::kIntelVpl) {
#if defined(USE_VPL_ENCODER)
        auto create_video_encoder = [](const webrtc::SdpVideoFormat& format) {
          return VplVideoEncoder::Create(
              VplSession::Create(),
              webrtc::PayloadStringToCodecType(format.name));
        };
        encoder_factory_config.encoders.push_back(
            VideoEncoderConfig(codec.type, create_video_encoder, 16));
#endif
      } else if (*codec.encoder ==
                 VideoCodecImplementation::kNvidiaVideoCodecSdk) {
#if defined(USE_NVCODEC_ENCODER)
        // CudaContext は必須ではない（Windows エンコーダでは DirectX を利用する）ので assert しない
        // assert(config.capability_config.cuda_context);
        auto create_video_encoder = [cuda_context =
                                         config.capability_config.cuda_context](
                                        const webrtc::SdpVideoFormat& format) {
          auto type = webrtc::PayloadStringToCodecType(format.name);
          auto cuda_type =
              type == webrtc::kVideoCodecVP8    ? CudaVideoCodec::VP8
              : type == webrtc::kVideoCodecVP9  ? CudaVideoCodec::VP9
              : type == webrtc::kVideoCodecH264 ? CudaVideoCodec::H264
              : type == webrtc::kVideoCodecH265 ? CudaVideoCodec::H265
              : type == webrtc::kVideoCodecAV1  ? CudaVideoCodec::AV1
                                                : CudaVideoCodec::JPEG;
          return NvCodecVideoEncoder::Create(cuda_context, cuda_type);
        };
        encoder_factory_config.encoders.push_back(
            VideoEncoderConfig(codec.type, create_video_encoder, 16));
#endif
      } else if (*codec.encoder == VideoCodecImplementation::kAmdAmf) {
#if defined(USE_AMF_ENCODER)
        assert(config.capability_config.amf_context);
        auto create_video_encoder = [amf_context =
                                         config.capability_config.amf_context](
                                        const webrtc::SdpVideoFormat& format) {
          auto type = webrtc::PayloadStringToCodecType(format.name);
          return AMFVideoEncoder::Create(amf_context, type);
        };
        encoder_factory_config.encoders.push_back(
            VideoEncoderConfig(codec.type, create_video_encoder, 16));
#endif
      } else if (IsCustomImplementation(*codec.encoder)) {
        auto create_video_encoder = [create_video_encoder =
                                         config.create_video_encoder,
                                     implementation = *codec.encoder,
                                     capability_config =
                                         config.capability_config](
                                        const webrtc::SdpVideoFormat& format) {
          auto type = webrtc::PayloadStringToCodecType(format.name);
          return create_video_encoder(implementation, capability_config, type);
        };
        encoder_factory_config.encoders.push_back(
            VideoEncoderConfig(codec.type, create_video_encoder, 16));
      }
    }
    if (codec.decoder) {
      if (*codec.decoder == VideoCodecImplementation::kInternal) {
        decoder_factory_config.decoders.push_back(
            VideoDecoderConfig(codec.type, builtin_decoder_factory));
      } else if (*codec.decoder == VideoCodecImplementation::kCiscoOpenH264) {
        assert(config.capability_config.openh264_path);
        auto create_video_decoder =
            [openh264_path = *config.capability_config.openh264_path](
                const webrtc::SdpVideoFormat& format) {
              return CreateOpenH264VideoDecoder(format, openh264_path);
            };
        decoder_factory_config.decoders.push_back(
            VideoDecoderConfig(codec.type, create_video_decoder));
      } else if (*codec.decoder == VideoCodecImplementation::kIntelVpl) {
#if defined(USE_VPL_ENCODER)
        auto create_video_decoder = [](const webrtc::SdpVideoFormat& format) {
          return VplVideoDecoder::Create(
              VplSession::Create(),
              webrtc::PayloadStringToCodecType(format.name));
        };
        decoder_factory_config.decoders.push_back(
            VideoDecoderConfig(codec.type, create_video_decoder));
#endif
      } else if (*codec.decoder ==
                 VideoCodecImplementation::kNvidiaVideoCodecSdk) {
#if defined(USE_NVCODEC_ENCODER)
        // CudaContext は必須ではない（Windows エンコーダでは DirectX を利用する）ので assert しない
        // assert(config.capability_config.cuda_context);
        auto create_video_decoder = [cuda_context =
                                         config.capability_config.cuda_context](
                                        const webrtc::SdpVideoFormat& format) {
          auto type = webrtc::PayloadStringToCodecType(format.name);
          auto cuda_type =
              type == webrtc::kVideoCodecVP8    ? CudaVideoCodec::VP8
              : type == webrtc::kVideoCodecVP9  ? CudaVideoCodec::VP9
              : type == webrtc::kVideoCodecH264 ? CudaVideoCodec::H264
              : type == webrtc::kVideoCodecH265 ? CudaVideoCodec::H265
              : type == webrtc::kVideoCodecAV1  ? CudaVideoCodec::AV1
                                                : CudaVideoCodec::JPEG;
          return NvCodecVideoDecoder::Create(cuda_context, cuda_type);
        };
        decoder_factory_config.decoders.push_back(
            VideoDecoderConfig(codec.type, create_video_decoder));
#endif
      } else if (*codec.decoder == VideoCodecImplementation::kAmdAmf) {
#if defined(USE_AMF_ENCODER)
        assert(config.capability_config.amf_context);
        auto create_video_decoder = [amf_context =
                                         config.capability_config.amf_context](
                                        const webrtc::SdpVideoFormat& format) {
          auto type = webrtc::PayloadStringToCodecType(format.name);
          return AMFVideoDecoder::Create(amf_context, type);
        };
        decoder_factory_config.decoders.push_back(
            VideoDecoderConfig(codec.type, create_video_decoder));
#endif
      } else if (IsCustomImplementation(*codec.decoder)) {
        auto create_video_decoder = [create_video_decoder =
                                         config.create_video_decoder,
                                     implementation = *codec.decoder,
                                     capability_config =
                                         config.capability_config](
                                        const webrtc::SdpVideoFormat& format) {
          auto type = webrtc::PayloadStringToCodecType(format.name);
          return create_video_decoder(implementation, capability_config, type);
        };
        decoder_factory_config.decoders.push_back(
            VideoDecoderConfig(codec.type, create_video_decoder));
      }
    }
  }

  SoraVideoCodecFactory factory;
  factory.encoder_factory =
      std::make_unique<SoraVideoEncoderFactory>(encoder_factory_config);
  factory.decoder_factory =
      std::make_unique<SoraVideoDecoderFactory>(decoder_factory_config);
  if (factory.encoder_factory == nullptr) {
    RTC_LOG(LS_ERROR) << "Failed to create SoraVideoEncoderFactory";
    return std::nullopt;
  }
  if (factory.decoder_factory == nullptr) {
    RTC_LOG(LS_ERROR) << "Failed to create SoraVideoDecoderFactory";
    return std::nullopt;
  }
  return factory;
}

}  // namespace sora
