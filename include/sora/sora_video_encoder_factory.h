#ifndef SORA_SORA_VIDEO_ENCODER_FACTORY_H_
#define SORA_SORA_VIDEO_ENCODER_FACTORY_H_

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

// WebRTC
#include <api/environment/environment.h>
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
  VideoEncoderConfig(std::shared_ptr<webrtc::VideoEncoderFactory> factory)
      : factory(std::move(factory)) {}
  // 指定した codec の場合のみ指定した factory を使ってエンコーダを設定する
  VideoEncoderConfig(webrtc::VideoCodecType codec,
                     std::shared_ptr<webrtc::VideoEncoderFactory> factory)
      : codec(codec), factory(std::move(factory)) {}

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

  /*
  エンコーダー内でビデオフレームバッファーを I420 に変換するかどうか

  force_i420_conversion = false を設定することでビデオフレームバッファーを I420 へ変換しなくなるため、 CPU への負荷が下がり、エンコードの性能が向上することがある。
  ただし、使用するバッファーの実装によっては、バッファーを複数回読みこむことができないため、サイマルキャストが利用できなくなる点に注意する必要がある。

  このフラグが必要になった背景は以下の通り
  - サイマルキャストは複数ストリームでエンコードするため同じバッファーを複数回読みこむ必要があるが、JetsonBuffer のような一部の kNative なバッファーの実装において、バッファーを複数回読み込めないという制限がある
  - Sora に type: connect で simulcast 有効/無効を指定して接続しても、Sora 接続後に受信する type: offer でサイマルキャストの有効/無効が上書きされるるため、サイマルキャストが有効であるかどうかは SoraVideoEncoderFactory を生成するタイミングでは分からないという制限がある
  - 上記の２つの制限によって、常に I420 への変換処理を走らせるのが安全な実装となる
  - しかしこの場合、非サイマルキャストで kNative なバッファーをエンコードする時にも I420 への変換が走ることになって、解像度や性能によってはフレームレートが出ないことがある
  - この I420 への変換は、 Sora の設定も含めて利用者が非サイマルキャストだと保証できる場合、あるいはサイマルキャストであっても複数回読める kNative なバッファーを利用している場合には不要な処理になる
  - そのような場合に I420 への変換を無効にするための設定として force_i420_conversion フラグが用意された
  */
  bool force_i420_conversion = true;

  // 内部用。触らないこと。
  bool is_internal = false;
};

class SoraVideoEncoderFactory : public webrtc::VideoEncoderFactory {
 public:
  SoraVideoEncoderFactory(SoraVideoEncoderFactoryConfig config);
  virtual ~SoraVideoEncoderFactory() {}

  std::vector<webrtc::SdpVideoFormat> GetSupportedFormats() const override;

  std::unique_ptr<webrtc::VideoEncoder> Create(
      const webrtc::Environment& env,
      const webrtc::SdpVideoFormat& format) override;

 private:
  // 一番内側のエンコーダを作る
  std::unique_ptr<webrtc::VideoEncoder> CreateInternalVideoEncoder(
      const webrtc::Environment& env,
      const webrtc::SdpVideoFormat& format,
      int& alignment);

 private:
  SoraVideoEncoderFactoryConfig config_;
  mutable std::vector<std::vector<webrtc::SdpVideoFormat>> formats_;
  std::unique_ptr<SoraVideoEncoderFactory> internal_encoder_factory_;
};

// ハードウェアエンコーダを出来るだけ使おうとして、見つからなければソフトウェアエンコーダを使う設定を返す
[[deprecated(
    "代わりに VideoCodecCapability, VideoCodecPreference を利用して下さい。")]]
SoraVideoEncoderFactoryConfig GetDefaultVideoEncoderFactoryConfig(
    std::shared_ptr<CudaContext> cuda_context = nullptr,
    void* env = nullptr,
    std::optional<std::string> openh264 = std::nullopt);
// ソフトウェアエンコーダのみを使う設定を返す
[[deprecated(
    "代わりに VideoCodecCapability, VideoCodecPreference を利用して下さい。")]]
SoraVideoEncoderFactoryConfig GetSoftwareOnlyVideoEncoderFactoryConfig(
    std::optional<std::string> openh264 = std::nullopt);

}  // namespace sora

#endif