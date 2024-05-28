#include "sora/sora_video_encoder_factory.h"

// WebRTC
#include <absl/memory/memory.h>
#include <absl/strings/match.h>
#include <api/environment/environment_factory.h>
#include <api/video_codecs/sdp_video_format.h>
#include <api/video_codecs/video_codec.h>
#include <api/video_codecs/vp9_profile.h>
#include <media/base/codec.h>
#include <media/base/media_constants.h>
#include <media/engine/simulcast_encoder_adapter.h>
#include <modules/video_coding/codecs/h264/include/h264.h>
#include <modules/video_coding/codecs/vp8/include/vp8.h>
#include <modules/video_coding/codecs/vp9/include/vp9.h>
#include <rtc_base/logging.h>

#if !defined(__arm__) || defined(__aarch64__) || defined(__ARM_NEON__)
#include <modules/video_coding/codecs/av1/libaom_av1_encoder.h>
#endif

#if defined(__APPLE__)
#include "sora/mac/mac_video_factory.h"
#endif

#if defined(SORA_CPP_SDK_ANDROID)
#include "sora/android/android_video_factory.h"
#endif

#if defined(USE_NVCODEC_ENCODER)
#include "sora/hwenc_nvcodec/nvcodec_h264_encoder.h"
#endif

#if defined(USE_VPL_ENCODER)
#include "sora/hwenc_vpl/vpl_video_encoder.h"
#endif

#if defined(USE_JETSON_ENCODER)
#include "sora/hwenc_jetson/jetson_video_encoder.h"
#endif

#include "default_video_formats.h"
#include "sora/aligned_encoder_adapter.h"
#include "sora/i420_encoder_adapter.h"
#include "sora/open_h264_video_encoder.h"

namespace sora {

SoraVideoEncoderFactory::SoraVideoEncoderFactory(
    SoraVideoEncoderFactoryConfig config)
    : config_(config) {
  if (config.use_simulcast_adapter) {
    auto config2 = config;
    config2.use_simulcast_adapter = false;
    config2.is_internal = true;
    internal_encoder_factory_.reset(new SoraVideoEncoderFactory(config2));
  }
}

std::vector<webrtc::SdpVideoFormat>
SoraVideoEncoderFactory::GetSupportedFormats() const {
  formats_.clear();

  std::vector<webrtc::SdpVideoFormat> r;
  for (auto& enc : config_.encoders) {
    // factory が定義されてればそれを使う
    // get_supported_formats が定義されてればそれを使う
    // どちらも無ければ codec ごとのデフォルト設定を利用する
    std::vector<webrtc::SdpVideoFormat> formats;
    if (enc.factory != nullptr) {
      formats = enc.factory->GetSupportedFormats();
    } else if (enc.get_supported_formats != nullptr) {
      formats = enc.get_supported_formats();
    } else {
      formats = GetDefaultVideoFormats(enc.codec);
    }
    r.insert(r.end(), formats.begin(), formats.end());
    formats_.push_back(formats);
  }
  return r;
}

std::unique_ptr<webrtc::VideoEncoder>
SoraVideoEncoderFactory::CreateInternalVideoEncoder(
    const webrtc::Environment& env,
    const webrtc::SdpVideoFormat& format,
    int& alignment) {
  if (formats_.empty()) {
    GetSupportedFormats();
  }

  webrtc::VideoCodecType specified_codec =
      webrtc::PayloadStringToCodecType(format.name);

  int n = 0;
  for (auto& enc : config_.encoders) {
    // 対応していないフォーマットを Create に渡した時の挙動は未定義なので
    // 確実に対応してるフォーマットのみを Create に渡すようにする。

    std::function<std::unique_ptr<webrtc::VideoEncoder>(
        const webrtc::Environment&, const webrtc::SdpVideoFormat&)>
        create_video_encoder;
    std::vector<webrtc::SdpVideoFormat> supported_formats = formats_[n++];

    if (enc.factory != nullptr) {
      create_video_encoder = [factory = enc.factory.get()](
                                 const webrtc::Environment& env,
                                 const webrtc::SdpVideoFormat& format) {
        return factory->Create(env, format);
      };
      // Factory 経由で作ったエンコーダはアライメントが必要かどうか分からないので全部アライメントする
      alignment = 16;
    } else if (enc.create_video_encoder != nullptr) {
      create_video_encoder = [&enc](const webrtc::Environment& env,
                                    const webrtc::SdpVideoFormat& format) {
        return enc.create_video_encoder(format);
      };
      alignment = enc.alignment;
    }

    for (const auto& f : supported_formats) {
      if (f.IsSameCodec(format)) {
        return create_video_encoder(env, format);
      }
    }
  }
  return nullptr;
}

std::unique_ptr<webrtc::VideoEncoder> SoraVideoEncoderFactory::Create(
    const webrtc::Environment& env,
    const webrtc::SdpVideoFormat& format) {
  if (internal_encoder_factory_ != nullptr) {
    // サイマルキャストの場合はアダプタを噛ましつつ、無条件ですべてアライメントする
    std::unique_ptr<webrtc::VideoEncoder> encoder =
        std::make_unique<webrtc::SimulcastEncoderAdapter>(
            env, internal_encoder_factory_.get(), nullptr, format);

    if (config_.force_i420_conversion_for_simulcast_adapter) {
      encoder = std::make_unique<I420EncoderAdapter>(std::move(encoder));
    }

    encoder =
        std::make_unique<AlignedEncoderAdapter>(std::move(encoder), 16, 16);
    return encoder;
  }

  int alignment = 0;
  auto encoder = CreateInternalVideoEncoder(env, format, alignment);
  if (encoder == nullptr) {
    return nullptr;
  }

  // この場合は呼び出し元でラップするのでここでは何もしない
  if (config_.is_internal) {
    return encoder;
  }

  // アライメントが必要ないなら何もしない
  if (alignment == 0) {
    return encoder;
  }

  // アライメント付きのエンコーダを利用する
  return std::unique_ptr<webrtc::VideoEncoder>(new AlignedEncoderAdapter(
      std::shared_ptr<webrtc::VideoEncoder>(std::move(encoder)), alignment,
      alignment));
}

SoraVideoEncoderFactoryConfig GetDefaultVideoEncoderFactoryConfig(
    std::shared_ptr<CudaContext> cuda_context,
    void* env,
    std::optional<std::string> openh264) {
  auto config = GetSoftwareOnlyVideoEncoderFactoryConfig(openh264);

#if defined(__APPLE__)
  config.encoders.insert(config.encoders.begin(),
                         VideoEncoderConfig(CreateMacVideoEncoderFactory()));
#endif

#if defined(SORA_CPP_SDK_ANDROID)
  if (env != nullptr) {
    config.encoders.insert(config.encoders.begin(),
                           VideoEncoderConfig(CreateAndroidVideoEncoderFactory(
                               static_cast<JNIEnv*>(env))));
  }
#endif

#if defined(USE_NVCODEC_ENCODER)
  if (NvCodecH264Encoder::IsSupported(cuda_context)) {
    config.encoders.insert(
        config.encoders.begin(),
        VideoEncoderConfig(
            webrtc::kVideoCodecH264,
            [cuda_context = cuda_context](
                auto format) -> std::unique_ptr<webrtc::VideoEncoder> {
              return NvCodecH264Encoder::Create(
                  cricket::CreateVideoCodec(format), cuda_context);
            },
            16));
  }
#endif

#if defined(USE_VPL_ENCODER)
  auto session = VplSession::Create();
  if (VplVideoEncoder::IsSupported(session, webrtc::kVideoCodecVP8)) {
    config.encoders.insert(
        config.encoders.begin(),
        VideoEncoderConfig(
            webrtc::kVideoCodecVP8,
            [](auto format) -> std::unique_ptr<webrtc::VideoEncoder> {
              return VplVideoEncoder::Create(VplSession::Create(),
                                             webrtc::kVideoCodecVP8);
            },
            16));
  }
  if (VplVideoEncoder::IsSupported(session, webrtc::kVideoCodecVP9)) {
    config.encoders.insert(
        config.encoders.begin(),
        VideoEncoderConfig(
            webrtc::kVideoCodecVP9,
            [](auto format) -> std::unique_ptr<webrtc::VideoEncoder> {
              return VplVideoEncoder::Create(VplSession::Create(),
                                             webrtc::kVideoCodecVP9);
            },
            16));
  }
  if (VplVideoEncoder::IsSupported(session, webrtc::kVideoCodecH264)) {
    config.encoders.insert(
        config.encoders.begin(),
        VideoEncoderConfig(
            webrtc::kVideoCodecH264,
            [](auto format) -> std::unique_ptr<webrtc::VideoEncoder> {
              return VplVideoEncoder::Create(VplSession::Create(),
                                             webrtc::kVideoCodecH264);
            },
            16));
  }
  if (VplVideoEncoder::IsSupported(session, webrtc::kVideoCodecH265)) {
    config.encoders.insert(
        config.encoders.begin(),
        VideoEncoderConfig(
            webrtc::kVideoCodecH265,
            [](auto format) -> std::unique_ptr<webrtc::VideoEncoder> {
              return VplVideoEncoder::Create(VplSession::Create(),
                                             webrtc::kVideoCodecH265);
            },
            16));
  }
  if (VplVideoEncoder::IsSupported(session, webrtc::kVideoCodecAV1)) {
    config.encoders.insert(
        config.encoders.begin(),
        VideoEncoderConfig(
            webrtc::kVideoCodecAV1,
            [](auto format) -> std::unique_ptr<webrtc::VideoEncoder> {
              return VplVideoEncoder::Create(VplSession::Create(),
                                             webrtc::kVideoCodecAV1);
            },
            16));
  }
#endif

#if defined(USE_JETSON_ENCODER)
  if (JetsonVideoEncoder::IsSupportedVP8()) {
    config.encoders.insert(config.encoders.begin(),
                           VideoEncoderConfig(
                               webrtc::kVideoCodecVP8,
                               [](auto format) {
                                 return std::unique_ptr<webrtc::VideoEncoder>(
                                     absl::make_unique<JetsonVideoEncoder>(
                                         cricket::CreateVideoCodec(format)));
                               },
                               16));
  }
  if (JetsonVideoEncoder::IsSupportedVP9()) {
    config.encoders.insert(config.encoders.begin(),
                           VideoEncoderConfig(
                               webrtc::kVideoCodecVP9,
                               [](auto format) {
                                 return std::unique_ptr<webrtc::VideoEncoder>(
                                     absl::make_unique<JetsonVideoEncoder>(
                                         cricket::CreateVideoCodec(format)));
                               },
                               16));
  }
  if (JetsonVideoEncoder::IsSupportedAV1()) {
    config.encoders.insert(config.encoders.begin(),
                           VideoEncoderConfig(
                               webrtc::kVideoCodecAV1,
                               [](auto format) {
                                 return std::unique_ptr<webrtc::VideoEncoder>(
                                     absl::make_unique<JetsonVideoEncoder>(
                                         cricket::CreateVideoCodec(format)));
                               },
                               16));
  }
  config.encoders.insert(config.encoders.begin(),
                         VideoEncoderConfig(
                             webrtc::kVideoCodecH264,
                             [](auto format) {
                               return std::unique_ptr<webrtc::VideoEncoder>(
                                   absl::make_unique<JetsonVideoEncoder>(
                                       cricket::CreateVideoCodec(format)));
                             },
                             16));
#endif

  return config;
}

SoraVideoEncoderFactoryConfig GetSoftwareOnlyVideoEncoderFactoryConfig(
    std::optional<std::string> openh264) {
  SoraVideoEncoderFactoryConfig config;
  config.encoders.push_back(
      VideoEncoderConfig(webrtc::kVideoCodecVP8, [](auto format) {
        return webrtc::CreateVp8Encoder(webrtc::CreateEnvironment());
      }));
  config.encoders.push_back(
      VideoEncoderConfig(webrtc::kVideoCodecVP9, [](auto format) {
        return webrtc::CreateVp9Encoder(webrtc::CreateEnvironment());
      }));
  if (openh264) {
    config.encoders.push_back(VideoEncoderConfig(
        webrtc::kVideoCodecH264, [openh264 = *openh264](auto format) {
          return CreateOpenH264VideoEncoder(format, openh264);
        }));
  }
#if !defined(__arm__) || defined(__aarch64__) || defined(__ARM_NEON__)
  config.encoders.push_back(VideoEncoderConfig(
      webrtc::kVideoCodecAV1,
      [](auto format) { return webrtc::CreateLibaomAv1Encoder(webrtc::CreateEnvironment()); }));
#endif
  return config;
}

}  // namespace sora
