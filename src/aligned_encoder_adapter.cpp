#include "sora/aligned_encoder_adapter.h"

#include <rtc_base/logging.h>

namespace sora {

static int Align(int size, int alignment) {
  return size - (size % alignment);
}

AlignedEncoderAdapter::AlignedEncoderAdapter(
    std::shared_ptr<webrtc::VideoEncoder> encoder,
    int horizontal_alignment,
    int vertical_alignment)
    : encoder_(encoder),
      horizontal_alignment_(horizontal_alignment),
      vertical_alignment_(vertical_alignment) {}

void AlignedEncoderAdapter::SetFecControllerOverride(
    webrtc::FecControllerOverride* fec_controller_override) {
  encoder_->SetFecControllerOverride(fec_controller_override);
}
int AlignedEncoderAdapter::Release() {
  return encoder_->Release();
}
int AlignedEncoderAdapter::InitEncode(
    const webrtc::VideoCodec* codec_settings,
    const webrtc::VideoEncoder::Settings& settings) {
  auto cs = *codec_settings;
  cs.width = Align(cs.width, horizontal_alignment_);
  cs.height = Align(cs.height, vertical_alignment_);
  for (int i = 0; i < cs.numberOfSimulcastStreams; i++) {
    cs.simulcastStream[i].width =
        Align(cs.simulcastStream[i].width, horizontal_alignment_);
    cs.simulcastStream[i].height =
        Align(cs.simulcastStream[i].height, vertical_alignment_);
  }
  return encoder_->InitEncode(&cs, settings);
}
int AlignedEncoderAdapter::Encode(
    const webrtc::VideoFrame& input_image,
    const std::vector<webrtc::VideoFrameType>* frame_types) {
  auto frame = input_image;
  auto crop_x = frame.width() % horizontal_alignment_;
  auto crop_y = frame.height() % vertical_alignment_;
  auto crop_width = frame.width() - crop_x;
  auto crop_height = frame.height() - crop_y;
  if (crop_x != 0 || crop_y != 0) {
    auto buffer = frame.video_frame_buffer()->CropAndScale(
        crop_x / 2, crop_y / 2, crop_width, crop_height, crop_width,
        crop_height);
    frame.set_video_frame_buffer(buffer);
  }
  return encoder_->Encode(frame, frame_types);
}

int AlignedEncoderAdapter::RegisterEncodeCompleteCallback(
    webrtc::EncodedImageCallback* callback) {
  return encoder_->RegisterEncodeCompleteCallback(callback);
}
void AlignedEncoderAdapter::SetRates(const RateControlParameters& parameters) {
  encoder_->SetRates(parameters);
}
void AlignedEncoderAdapter::OnPacketLossRateUpdate(float packet_loss_rate) {
  encoder_->OnPacketLossRateUpdate(packet_loss_rate);
}
void AlignedEncoderAdapter::OnRttUpdate(int64_t rtt_ms) {
  encoder_->OnRttUpdate(rtt_ms);
}
void AlignedEncoderAdapter::OnLossNotification(
    const LossNotification& loss_notification) {
  encoder_->OnLossNotification(loss_notification);
}

webrtc::VideoEncoder::EncoderInfo AlignedEncoderAdapter::GetEncoderInfo()
    const {
  return encoder_->GetEncoderInfo();
}

}  // namespace sora