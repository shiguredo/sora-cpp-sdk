#ifndef SORA_HWENC_XCODER_XCODER_VIDEO_DECODER_H_
#define SORA_HWENC_XCODER_XCODER_VIDEO_DECODER_H_

#include <memory>
#include <optional>

// WebRTC
#include <api/video/video_codec_type.h>
#include <api/video_codecs/video_decoder.h>

namespace sora {

class LibxcoderVideoDecoder : public webrtc::VideoDecoder {
 public:
  static bool IsSupported(webrtc::VideoCodecType codec);
  static std::unique_ptr<LibxcoderVideoDecoder> Create(
      webrtc::VideoCodecType codec);
};

}  // namespace sora

#endif
