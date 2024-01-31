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
  // 指定したコーデックに対応するエンコーダを設定する
  VideoEncoderConfig(webrtc::VideoCodecType codec,
                     std::function<std::unique_ptr<webrtc::VideoEncoder>(
                         const webrtc::SdpVideoFormat&)> create_video_encoder,
                     int alignment = 0)
      : codec(codec),
        create_video_encoder(std::move(create_video_encoder)),
        alignment(alignment) {}
  // 特定の SdpVideoFormat に対応するエンコーダを設定する
  // コーデック指定だと物足りない人向け
  VideoEncoderConfig(std::function<std::vector<webrtc::SdpVideoFormat>()>
                         get_supported_formats,
                     std::function<std::unique_ptr<webrtc::VideoEncoder>(
                         const webrtc::SdpVideoFormat&)> create_video_encoder,
                     int alignment = 0)
      : get_supported_formats(std::move(get_supported_formats)),
        create_video_encoder(std::move(create_video_encoder)),
        alignment(alignment) {}
  // 指定した factory を使ってエンコーダを設定する
  VideoEncoderConfig(std::unique_ptr<webrtc::VideoEncoderFactory> factory)
      : factory(std::move(factory)) {}

  webrtc::VideoCodecType codec = webrtc::VideoCodecType::kVideoCodecGeneric;
  std::function<std::vector<webrtc::SdpVideoFormat>()> get_supported_formats;
  std::function<std::unique_ptr<webrtc::VideoEncoder>(
      const webrtc::SdpVideoFormat&)>
      create_video_encoder;
  std::shared_ptr<webrtc::VideoEncoderFactory> factory;
  int alignment = 0;
};

struct SoraVideoEncoderFactoryConfig {
  // 指定されたコーデックに対して、どのエンコーダを利用するかの設定
  // encoders の 0 番目から順番に一致するコーデックを探して、見つかったらそれを利用する
  std::vector<VideoEncoderConfig> encoders;
  // webrtc::SimulcastEncoderAdapter を噛ますかどうか
  bool use_simulcast_adapter = false;

  /*
  サイマルキャスト利用時 (use_simulcast_adapter = true) に、エンコーダー内でフレーム・バッファーを I420 に変換するかどうか
  false に設定すると CPU への負荷が下がり、エンコードの性能が向上するが、一部の kNative なフレーム・バッファーででサイマルキャストが利用できなくなる

  このフラグが必要になった背景は以下の通り
  - 一部の kNative なフレーム・バッファーでは、バッファーを複数回読み込めないという制限があり、サイマルキャストで問題になった
    - サイマルキャストは複数ストリームを送信するため、バッファーを複数回読みこむ必要がある
  - フレーム・バッファーを I420 に変換することでバッファーが複数回読めるようになったが、変換処理が重く、エンコーダーの性能が低下した
  - 当初はサイマルキャストを無効 (use_simulcast_adapter = false) にすることで、問題を回避できると考えていたが、一部のケースで問題があることがわかった
    - Sora の type: offer でサイマルキャストの有効/無効を上書きして設定できるため、 use_simulcast_adapter = false を設定するとそのケースに対応できない
  - そのため、 I420 への変換処理の有効/無効を制御するフラグが必要になった
  */
  bool force_simulcast_i420_conversion = true;

  // 内部用。触らないこと。
  bool is_internal = false;
};

class SoraVideoEncoderFactory : public webrtc::VideoEncoderFactory {
 public:
  SoraVideoEncoderFactory(SoraVideoEncoderFactoryConfig config);
  virtual ~SoraVideoEncoderFactory() {}

  std::vector<webrtc::SdpVideoFormat> GetSupportedFormats() const override;

  std::unique_ptr<webrtc::VideoEncoder> CreateVideoEncoder(
      const webrtc::SdpVideoFormat& format) override;

 private:
  // 一番内側のエンコーダを作る
  std::unique_ptr<webrtc::VideoEncoder> CreateInternalVideoEncoder(
      const webrtc::SdpVideoFormat& format,
      int& alignment);

 private:
  SoraVideoEncoderFactoryConfig config_;
  mutable std::vector<std::vector<webrtc::SdpVideoFormat>> formats_;
  std::unique_ptr<SoraVideoEncoderFactory> internal_encoder_factory_;
};

// ハードウェアエンコーダを出来るだけ使おうとして、見つからなければソフトウェアエンコーダを使う設定を返す
SoraVideoEncoderFactoryConfig GetDefaultVideoEncoderFactoryConfig(
    std::shared_ptr<CudaContext> cuda_context = nullptr,
    void* env = nullptr);
// ソフトウェアエンコーダのみを使う設定を返す
SoraVideoEncoderFactoryConfig GetSoftwareOnlyVideoEncoderFactoryConfig();

}  // namespace sora

#endif