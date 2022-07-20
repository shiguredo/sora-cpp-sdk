#include "sora/i420_encoder_adapter.h"

#include <rtc_base/logging.h>

namespace sora {

I420EncoderAdapter::I420EncoderAdapter(
    std::shared_ptr<webrtc::VideoEncoder> encoder)
    : encoder_(encoder) {}

void I420EncoderAdapter::SetFecControllerOverride(
    webrtc::FecControllerOverride* fec_controller_override) {
  encoder_->SetFecControllerOverride(fec_controller_override);
}
int I420EncoderAdapter::Release() {
  return encoder_->Release();
}
int I420EncoderAdapter::InitEncode(
    const webrtc::VideoCodec* codec_settings,
    const webrtc::VideoEncoder::Settings& settings) {
  return encoder_->InitEncode(codec_settings, settings);
}
int I420EncoderAdapter::Encode(
    const webrtc::VideoFrame& input_image,
    const std::vector<webrtc::VideoFrameType>* frame_types) {
  auto frame = input_image;
  auto buffer = frame.video_frame_buffer();
  if (buffer->type() == webrtc::VideoFrameBuffer::Type::kNative) {
    frame.set_video_frame_buffer(buffer->ToI420());
  }
  return encoder_->Encode(frame, frame_types);
}

int I420EncoderAdapter::RegisterEncodeCompleteCallback(
    webrtc::EncodedImageCallback* callback) {
  return encoder_->RegisterEncodeCompleteCallback(callback);
}
void I420EncoderAdapter::SetRates(const RateControlParameters& parameters) {
  encoder_->SetRates(parameters);
}
void I420EncoderAdapter::OnPacketLossRateUpdate(float packet_loss_rate) {
  encoder_->OnPacketLossRateUpdate(packet_loss_rate);
}
void I420EncoderAdapter::OnRttUpdate(int64_t rtt_ms) {
  encoder_->OnRttUpdate(rtt_ms);
}
void I420EncoderAdapter::OnLossNotification(
    const LossNotification& loss_notification) {
  encoder_->OnLossNotification(loss_notification);
}

webrtc::VideoEncoder::EncoderInfo I420EncoderAdapter::GetEncoderInfo() const {
  return encoder_->GetEncoderInfo();
}

}  // namespace sora