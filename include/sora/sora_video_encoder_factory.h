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
  use_simulcast_adapter = true の際に、エンコーダー内でビデオ・フレームのバッファーを I420 に変換するかどうか

  force_i420_conversion_for_simulcast_adapter = false を設定することで I420 への変換を実行しなくなるため、 CPU への負荷が下がり、エンコードの性能が向上する
  ただし、使用するバッファーの実装によっては、バッファーを複数回読みこむことができないため、サイマルキャストが利用できなくなる

  このフラグが必要になった背景は以下の通り
  - サイマルキャスト時、 JetsonBuffer のような一部の kNative なバッファーの実装において、バッファーを複数回読み込めないという制限があるため、 I420 への変換が必要になる
    - サイマルキャストは複数ストリームを送信するため、バッファーを複数回読みこむ必要がある
  - サイマルキャスト時は use_simulcast_adapter = true にしてサイマルキャストアダプタを利用する必要があるが、 SoraClientContext の実装ではサイマルキャスト時でも非サイマルキャスト時でも常に use_simulcast_adapter = true として SoraVideoEncoderFactory を生成している
    - Sora に type: connect で simulcast 有効/無効を指定して接続しても、Sora 接続後に受信する type: offer でサイマルキャストの有効/無効が上書きできるため、SoraClientContext 生成時に use_simulcast_adapter の値をどうするべきか決定できない。そのため常に use_simulcast_adapter = true にしておくのが安全となる
      - 非サイマルキャスト時に use_simulcast_adapter = true とするのは、パフォーマンスの問題はあっても動作に影響は無い
  - 上記の２つの問題によって、非サイマルキャスト時でも I420 への変換処理が走ることになって、解像度や性能によってはフレームレートが出ないことがある
  - この I420 への変換は、 Sora の設定も含めて利用者が非サイマルキャストだと保証できる場合、あるいはサイマルキャストであっても複数回読める kNative なバッファーを利用している場合には不要な処理になる
  - そのような場合に I420 への変換を無効にすることで十分なフレームレートを確保できるようにするために、このフラグが必要になった
  */
  bool force_i420_conversion_for_simulcast_adapter = true;

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
    void* env = nullptr,
    std::optional<std::string> openh264 = std::nullopt);
// ソフトウェアエンコーダのみを使う設定を返す
SoraVideoEncoderFactoryConfig GetSoftwareOnlyVideoEncoderFactoryConfig(
    std::optional<std::string> openh264 = std::nullopt);

}  // namespace sora

#endif