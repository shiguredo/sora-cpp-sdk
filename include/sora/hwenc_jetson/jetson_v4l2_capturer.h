#ifndef SORA_HWENC_JETSON_JETSON_V4L2_CAPTURER_H_
#define SORA_HWENC_JETSON_JETSON_V4L2_CAPTURER_H_

#include "jetson_jpeg_decoder_pool.h"
#include "sora/v4l2/v4l2_video_capturer.h"

namespace sora {

class JetsonV4L2Capturer : public V4L2VideoCapturer {
 public:
  JetsonV4L2Capturer::JetsonV4L2Capturer(const V4L2VideoCapturerConfig& config);
  static rtc::scoped_refptr<V4L2VideoCapturer> Create(
      const V4L2VideoCapturerConfig& config);

 private:
  static rtc::scoped_refptr<V4L2VideoCapturer> Create(
      webrtc::VideoCaptureModule::DeviceInfo* device_info,
      const V4L2VideoCapturerConfig& config,
      size_t capture_device_index);

  bool AllocateVideoBuffers() override;
  bool DeAllocateVideoBuffers() override;
  void OnCaptured(uint8_t* data, uint32_t bytesused) override;

  std::shared_ptr<JetsonJpegDecoderPool> jpeg_decoder_pool_;
};

}  // namespace sora

#endif
