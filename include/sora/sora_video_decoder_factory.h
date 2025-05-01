#ifndef SORA_SORA_VIDEO_DECODER_FACTORY_H_
#define SORA_SORA_VIDEO_DECODER_FACTORY_H_

#include <memory>
#include <vector>

// WebRTC
#include <api/environment/environment.h>
#include <api/video/video_codec_type.h>
#include <api/video_codecs/video_decoder_factory.h>

#include "sora/cuda_context.h"

namespace sora {

struct VideoDecoderConfig {
  VideoDecoderConfig() = default;
  // 指定したコーデックに対応するデコーダを設定する
  VideoDecoderConfig(webrtc::VideoCodecType codec,
                     std::function<std::unique_ptr<webrtc::VideoDecoder>(
                         const webrtc::SdpVideoFormat&)> create_video_decoder)
      : codec(codec), create_video_decoder(std::move(create_video_decoder)) {}
  // 特定の SdpVideoFormat に対応するデコーダを設定する
  // コーデック指定だと物足りない人向け
  VideoDecoderConfig(std::function<std::vector<webrtc::SdpVideoFormat>()>
                         get_supported_formats,
                     std::function<std::unique_ptr<webrtc::VideoDecoder>(
                         const webrtc::SdpVideoFormat&)> create_video_decoder)
      : get_supported_formats(std::move(get_supported_formats)),
        create_video_decoder(std::move(create_video_decoder)) {}
  // 指定した factory を使ってデコーダを設定する
  VideoDecoderConfig(std::shared_ptr<webrtc::VideoDecoderFactory> factory)
      : factory(std::move(factory)) {}
  // 指定した codec の場合のみ指定した factory を使ってデコーダを設定する
  VideoDecoderConfig(webrtc::VideoCodecType codec,
                     std::shared_ptr<webrtc::VideoDecoderFactory> factory)
      : codec(codec), factory(std::move(factory)) {}

  webrtc::VideoCodecType codec = webrtc::VideoCodecType::kVideoCodecGeneric;
  std::function<std::vector<webrtc::SdpVideoFormat>()> get_supported_formats;
  std::function<std::unique_ptr<webrtc::VideoDecoder>(
      const webrtc::SdpVideoFormat&)>
      create_video_decoder;
  std::shared_ptr<webrtc::VideoDecoderFactory> factory;
};

struct SoraVideoDecoderFactoryConfig {
  // 指定されたコーデックに対して、どのデコーダを利用するかの設定
  // decoders の 0 番目から順番に一致するコーデックを探して、見つかったらそれを利用する
  std::vector<VideoDecoderConfig> decoders;
};

class SoraVideoDecoderFactory : public webrtc::VideoDecoderFactory {
 public:
  SoraVideoDecoderFactory(SoraVideoDecoderFactoryConfig config);
  virtual ~SoraVideoDecoderFactory() {}

  std::vector<webrtc::SdpVideoFormat> GetSupportedFormats() const override;

  std::unique_ptr<webrtc::VideoDecoder> Create(
      const webrtc::Environment& env,
      const webrtc::SdpVideoFormat& format) override;

 private:
  SoraVideoDecoderFactoryConfig config_;
  mutable std::vector<std::vector<webrtc::SdpVideoFormat>> formats_;
};

// ハードウェアデコーダを出来るだけ使おうとして、見つからなければソフトウェアデコーダを使う設定を返す
[[deprecated(
    "代わりに VideoCodecCapability, VideoCodecPreference を利用して下さい。")]]
SoraVideoDecoderFactoryConfig GetDefaultVideoDecoderFactoryConfig(
    std::shared_ptr<CudaContext> cuda_context = nullptr,
    void* env = nullptr);
// ソフトウェアデコーダのみを使う設定を返す
[[deprecated(
    "代わりに VideoCodecCapability, VideoCodecPreference を利用して下さい。")]]
SoraVideoDecoderFactoryConfig GetSoftwareOnlyVideoDecoderFactoryConfig();

}  // namespace sora

#endif
