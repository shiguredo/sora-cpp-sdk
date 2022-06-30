#include "sora/sora_video_decoder_factory.h"

// WebRTC
#include <absl/strings/match.h>
#include <api/video_codecs/sdp_video_format.h>
#include <media/base/codec.h>
#include <media/base/media_constants.h>
#include <modules/video_coding/codecs/h264/include/h264.h>
#include <modules/video_coding/codecs/vp8/include/vp8.h>
#include <modules/video_coding/codecs/vp9/include/vp9.h>
#include <rtc_base/checks.h>
#include <rtc_base/logging.h>

#if !defined(__arm__) || defined(__aarch64__) || defined(__ARM_NEON__)
#include <modules/video_coding/codecs/av1/libaom_av1_decoder.h>
#endif

#if defined(__APPLE__)
#include "sora/mac/mac_video_factory.h"
#endif

#if defined(SORA_CPP_SDK_ANDROID)
#include "sora/android/android_video_factory.h"
#endif

#if USE_NVCODEC_ENCODER
#include "sora/hwenc_nvcodec/nvcodec_video_decoder.h"
#endif

#if USE_MSDK_ENCODER
#include "sora/hwenc_msdk/msdk_video_decoder.h"
#endif

#if USE_JETSON_ENCODER
#include "sora/hwenc_jetson/jetson_video_decoder.h"
#endif

#include "default_video_formats.h"

namespace sora {

SoraVideoDecoderFactory::SoraVideoDecoderFactory(
    SoraVideoDecoderFactoryConfig config)
    : config_(std::move(config)) {}

std::vector<webrtc::SdpVideoFormat>
SoraVideoDecoderFactory::GetSupportedFormats() const {
  formats_.clear();

  std::vector<webrtc::SdpVideoFormat> r;
  for (auto& enc : config_.decoders) {
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

std::unique_ptr<webrtc::VideoDecoder>
SoraVideoDecoderFactory::CreateVideoDecoder(
    const webrtc::SdpVideoFormat& format) {
  webrtc::VideoCodecType specified_codec =
      webrtc::PayloadStringToCodecType(format.name);

  int n = 0;
  for (auto& enc : config_.decoders) {
    // 対応していないフォーマットを CreateVideoDecoder に渡した時の挙動は未定義なので
    // 確実に対応してるフォーマットのみを CreateVideoDecoder に渡すようにする。

    std::function<std::unique_ptr<webrtc::VideoDecoder>(
        const webrtc::SdpVideoFormat&)>
        create_video_decoder;
    std::vector<webrtc::SdpVideoFormat> supported_formats = formats_[n++];

    if (enc.factory != nullptr) {
      create_video_decoder =
          [factory = enc.factory.get()](const webrtc::SdpVideoFormat& format) {
            return factory->CreateVideoDecoder(format);
          };
    } else if (enc.create_video_decoder != nullptr) {
      create_video_decoder = enc.create_video_decoder;
    }

    std::unique_ptr<webrtc::VideoDecoder> r;
    for (const auto& f : supported_formats) {
      if (f.IsSameCodec(format)) {
        return create_video_decoder(format);
      }
    }

    if (r != nullptr) {
      return r;
    }
  }

  return nullptr;
}

SoraVideoDecoderFactoryConfig GetDefaultVideoDecoderFactoryConfig(
    std::shared_ptr<CudaContext> cuda_context,
    void* env) {
  auto config = GetSoftwareOnlyVideoDecoderFactoryConfig();

#if defined(__APPLE__)
  config.decoders.insert(config.decoders.begin(),
                         VideoDecoderConfig(CreateMacVideoDecoderFactory()));
#endif

#if defined(SORA_CPP_SDK_ANDROID)
  if (env != nullptr) {
    config.decoders.insert(config.decoders.begin(),
                           VideoDecoderConfig(CreateAndroidVideoDecoderFactory(
                               static_cast<JNIEnv*>(env))));
  }
#endif

#if USE_NVCODEC_ENCODER
  if (NvCodecVideoDecoder::IsSupported(cuda_context,
                                       sora::CudaVideoCodec::VP8)) {
    config.decoders.insert(
        config.decoders.begin(),
        VideoDecoderConfig(webrtc::kVideoCodecVP8,
                           [cuda_context = cuda_context](auto format) {
                             return std::unique_ptr<webrtc::VideoDecoder>(
                                 absl::make_unique<NvCodecVideoDecoder>(
                                     cuda_context, CudaVideoCodec::VP8));
                           }));
  }
  if (NvCodecVideoDecoder::IsSupported(cuda_context,
                                       sora::CudaVideoCodec::VP9)) {
    config.decoders.insert(
        config.decoders.begin(),
        VideoDecoderConfig(webrtc::kVideoCodecVP9,
                           [cuda_context = cuda_context](auto format) {
                             return std::unique_ptr<webrtc::VideoDecoder>(
                                 absl::make_unique<NvCodecVideoDecoder>(
                                     cuda_context, CudaVideoCodec::VP9));
                           }));
  }
  if (NvCodecVideoDecoder::IsSupported(cuda_context,
                                       sora::CudaVideoCodec::H264)) {
    config.decoders.insert(
        config.decoders.begin(),
        VideoDecoderConfig(webrtc::kVideoCodecH264,
                           [cuda_context = cuda_context](auto format) {
                             return std::unique_ptr<webrtc::VideoDecoder>(
                                 absl::make_unique<NvCodecVideoDecoder>(
                                     cuda_context, CudaVideoCodec::H264));
                           }));
  }
#endif

#if USE_MSDK_ENCODER
  auto session = MsdkSession::Create();
  if (MsdkVideoDecoder::IsSupported(session, webrtc::kVideoCodecVP8)) {
    config.decoders.insert(
        config.decoders.begin(),
        VideoDecoderConfig(
            webrtc::kVideoCodecVP8,
            [](auto format) -> std::unique_ptr<webrtc::VideoDecoder> {
              return MsdkVideoDecoder::Create(MsdkSession::Create(),
                                              webrtc::kVideoCodecVP8);
            }));
  }
  if (MsdkVideoDecoder::IsSupported(session, webrtc::kVideoCodecVP9)) {
    config.decoders.insert(
        config.decoders.begin(),
        VideoDecoderConfig(
            webrtc::kVideoCodecVP9,
            [](auto format) -> std::unique_ptr<webrtc::VideoDecoder> {
              return MsdkVideoDecoder::Create(MsdkSession::Create(),
                                              webrtc::kVideoCodecVP9);
            }));
  }
  if (MsdkVideoDecoder::IsSupported(session, webrtc::kVideoCodecH264)) {
    config.decoders.insert(
        config.decoders.begin(),
        VideoDecoderConfig(
            webrtc::kVideoCodecH264,
            [](auto format) -> std::unique_ptr<webrtc::VideoDecoder> {
              return MsdkVideoDecoder::Create(MsdkSession::Create(),
                                              webrtc::kVideoCodecH264);
            }));
  }
  if (MsdkVideoDecoder::IsSupported(session, webrtc::kVideoCodecAV1)) {
    config.decoders.insert(
        config.decoders.begin(),
        VideoDecoderConfig(
            webrtc::kVideoCodecAV1,
            [](auto format) -> std::unique_ptr<webrtc::VideoDecoder> {
              return MsdkVideoDecoder::Create(MsdkSession::Create(),
                                              webrtc::kVideoCodecAV1);
            }));
  }
#endif

#if USE_JETSON_ENCODER
  if (JetsonVideoDecoder::IsSupportedVP8()) {
    config.decoders.insert(
        config.decoders.begin(),
        VideoDecoderConfig(webrtc::kVideoCodecVP8, [](auto format) {
          return std::unique_ptr<webrtc::VideoDecoder>(
              absl::make_unique<JetsonVideoDecoder>(webrtc::kVideoCodecVP8));
        }));
  }
  if (JetsonVideoDecoder::IsSupportedAV1()) {
    config.decoders.insert(
        config.decoders.begin(),
        VideoDecoderConfig(webrtc::kVideoCodecAV1, [](auto format) {
          return std::unique_ptr<webrtc::VideoDecoder>(
              absl::make_unique<JetsonVideoDecoder>(webrtc::kVideoCodecAV1));
        }));
  }
  config.decoders.insert(
      config.decoders.begin(),
      VideoDecoderConfig(webrtc::kVideoCodecVP9, [](auto format) {
        return std::unique_ptr<webrtc::VideoDecoder>(
            absl::make_unique<JetsonVideoDecoder>(webrtc::kVideoCodecVP9));
      }));
  config.decoders.insert(
      config.decoders.begin(),
      VideoDecoderConfig(webrtc::kVideoCodecH264, [](auto format) {
        return std::unique_ptr<webrtc::VideoDecoder>(
            absl::make_unique<JetsonVideoDecoder>(webrtc::kVideoCodecH264));
      }));
#endif

  return config;
}

SoraVideoDecoderFactoryConfig GetSoftwareOnlyVideoDecoderFactoryConfig() {
  SoraVideoDecoderFactoryConfig config;
  config.decoders.push_back(VideoDecoderConfig(
      webrtc::kVideoCodecVP8,
      [](auto format) { return webrtc::VP8Decoder::Create(); }));
  config.decoders.push_back(VideoDecoderConfig(
      webrtc::kVideoCodecVP9,
      [](auto format) { return webrtc::VP9Decoder::Create(); }));
#if !defined(__arm__) || defined(__aarch64__) || defined(__ARM_NEON__)
  config.decoders.push_back(VideoDecoderConfig(
      webrtc::kVideoCodecAV1,
      [](auto format) { return webrtc::CreateLibaomAv1Decoder(); }));
#endif
  return config;
}

}  // namespace sora
