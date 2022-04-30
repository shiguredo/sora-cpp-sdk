#ifndef SORA_SORA_VIDEO_ENCODER_FACTORY_H_
#define SORA_SORA_VIDEO_ENCODER_FACTORY_H_

#include <memory>
#include <vector>

// WebRTC
#include <api/video/video_codec_type.h>
#include <api/video_codecs/sdp_video_format.h>
#include <api/video_codecs/video_encoder.h>
#include <api/video_codecs/video_encoder_factory.h>

#include "sora/cuda_context.h"

namespace sora {

struct VideoEncoderConfig {
  VideoEncoderConfig() = default;
  VideoEncoderConfig(webrtc::VideoCodecType codec,
                     std::function<std::unique_ptr<webrtc::VideoEncoder>(
                         const webrtc::SdpVideoFormat&)> create_video_encoder)
      : codec(codec), create_video_encoder(std::move(create_video_encoder)) {}
  VideoEncoderConfig(std::function<std::vector<webrtc::SdpVideoFormat>()>
                         get_supported_formats,
                     std::function<std::unique_ptr<webrtc::VideoEncoder>(
                         const webrtc::SdpVideoFormat&)> create_video_encoder)
      : get_supported_formats(std::move(get_supported_formats)),
        create_video_encoder(std::move(create_video_encoder)) {}
  VideoEncoderConfig(std::unique_ptr<webrtc::VideoEncoderFactory> factory)
      : factory(std::move(factory)) {}

  webrtc::VideoCodecType codec = webrtc::VideoCodecType::kVideoCodecGeneric;
  std::function<std::vector<webrtc::SdpVideoFormat>()> get_supported_formats;
  std::function<std::unique_ptr<webrtc::VideoEncoder>(
      const webrtc::SdpVideoFormat&)>
      create_video_encoder;
  std::shared_ptr<webrtc::VideoEncoderFactory> factory;
};

struct SoraVideoEncoderFactoryConfig {
  std::vector<VideoEncoderConfig> encoders;
  bool use_simulcast_adapter = false;
};

class SoraVideoEncoderFactory : public webrtc::VideoEncoderFactory {
 public:
  SoraVideoEncoderFactory(SoraVideoEncoderFactoryConfig config);
  virtual ~SoraVideoEncoderFactory() {}

  std::vector<webrtc::SdpVideoFormat> GetSupportedFormats() const override;

  std::unique_ptr<webrtc::VideoEncoder> CreateVideoEncoder(
      const webrtc::SdpVideoFormat& format) override;

 private:
  SoraVideoEncoderFactoryConfig config_;
  mutable std::vector<std::vector<webrtc::SdpVideoFormat>> formats_;
  std::unique_ptr<SoraVideoEncoderFactory> internal_encoder_factory_;
};

// ハードウェアエンコーダを出来るだけ使おうとして、見つからなければソフトウェアエンコーダを使う設定を返す
SoraVideoEncoderFactoryConfig GetDefaultVideoEncoderFactoryConfig(
    std::shared_ptr<CudaContext> cuda_context);
// ソフトウェアエンコーダのみを使う設定を返す
SoraVideoEncoderFactoryConfig GetSoftwareOnlyVideoEncoderFactoryConfig();

}  // namespace sora

#endif