#include "sora/hwenc_xcoder/xcoder_video_decoder.h"

#include <iostream>
#include <queue>
#include <thread>

// WebRTC
#include <api/video_codecs/video_decoder.h>
#include <common_video/include/video_frame_buffer_pool.h>
#include <modules/video_coding/include/video_error_codes.h>
#include <rtc_base/checks.h>
#include <rtc_base/logging.h>
#include <rtc_base/platform_thread.h>
#include <rtc_base/time_utils.h>
#include <third_party/libyuv/include/libyuv/convert.h>

extern "C" {
#include <ni_device_api.h>
}

namespace sora {

class LibxcoderVideoDecoderImpl : public LibxcoderVideoDecoder {
 public:
  LibxcoderVideoDecoderImpl(webrtc::VideoCodecType codec);
  ~LibxcoderVideoDecoderImpl() override;

  bool Configure(const Settings& settings) override;

  int32_t Decode(const webrtc::EncodedImage& input_image,
                 bool missing_frames,
                 int64_t render_time_ms) override;

  int32_t RegisterDecodeCompleteCallback(
      webrtc::DecodedImageCallback* callback) override;

  int32_t Release() override;

  const char* ImplementationName() const override;
};

////////////////////////
// LibxcoderVideoDecoder
////////////////////////

bool LibxcoderVideoDecoder::IsSupported(webrtc::VideoCodecType codec) {
  return false;
}

std::unique_ptr<LibxcoderVideoDecoder> LibxcoderVideoDecoder::Create(
    webrtc::VideoCodecType codec) {
  return std::unique_ptr<LibxcoderVideoDecoder>(
      new LibxcoderVideoDecoderImpl(codec));
}

}  // namespace sora
