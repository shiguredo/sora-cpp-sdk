#include "sora/hwenc_jetson/jetson_jpeg_decoder_pool.h"

// WebRTC
#include <rtc_base/logging.h>

// Jetson Linux Multimedia API
#include <NvJpegDecoder.h>

namespace sora {

std::shared_ptr<JetsonJpegDecoder> JetsonJpegDecoderPool::Pop() {
  std::shared_ptr<NvJPEGDecoder> nv_decoder;

  {
    std::lock_guard<std::mutex> lock(mtx_);
    if (decoder_queue_.size() == 0) {
      nv_decoder.reset(NvJPEGDecoder::createJPEGDecoder("jpegdec"));
    } else {
      nv_decoder = std::move(decoder_queue_.front());
      decoder_queue_.pop();
    }
  }

  std::shared_ptr<JetsonJpegDecoder> decoder(
      new JetsonJpegDecoder(shared_from_this(), std::move(nv_decoder)));
  return decoder;
}

void JetsonJpegDecoderPool::Push(std::shared_ptr<NvJPEGDecoder> decoder) {
  std::lock_guard<std::mutex> lock(mtx_);
  decoder_queue_.push(std::move(decoder));
}

}  // namespace sora
