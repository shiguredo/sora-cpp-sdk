#ifndef SORA_I420_ENCODER_ADAPTER_H_
#define SORA_I420_ENCODER_ADAPTER_H_

#include <memory>

// WebRTC
#include <api/video_codecs/video_encoder.h>

namespace sora {

// kNative なフレームを I420 にするアダプタ
class I420EncoderAdapter : public webrtc::VideoEncoder {
 public:
  I420EncoderAdapter(std::shared_ptr<webrtc::VideoEncoder> encoder);

  void SetFecControllerOverride(
      webrtc::FecControllerOverride* fec_controller_override) override;
  int Release() override;
  int InitEncode(const webrtc::VideoCodec* codec_settings,
                 const webrtc::VideoEncoder::Settings& settings) override;
  int Encode(const webrtc::VideoFrame& input_image,
             const std::vector<webrtc::VideoFrameType>* frame_types) override;
  int RegisterEncodeCompleteCallback(
      webrtc::EncodedImageCallback* callback) override;
  void SetRates(const RateControlParameters& parameters) override;
  void OnPacketLossRateUpdate(float packet_loss_rate) override;
  void OnRttUpdate(int64_t rtt_ms) override;
  void OnLossNotification(const LossNotification& loss_notification) override;
  EncoderInfo GetEncoderInfo() const override;

 private:
  std::shared_ptr<webrtc::VideoEncoder> encoder_;
};

}  // namespace sora
#endif