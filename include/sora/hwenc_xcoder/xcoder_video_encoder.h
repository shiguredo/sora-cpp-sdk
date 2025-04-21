#ifndef SORA_HWENC_XCODER_XCODER_VIDEO_ENCODER_H_
#define SORA_HWENC_XCODER_XCODER_VIDEO_ENCODER_H_

#include <memory>

// WebRTC
#include <api/video/video_codec_type.h>
#include <api/video_codecs/video_encoder.h>

namespace sora {

class LibxcoderVideoEncoder : public webrtc::VideoEncoder {
 public:
  static bool IsSupported(webrtc::VideoCodecType codec);
  static std::unique_ptr<LibxcoderVideoEncoder> Create(
      webrtc::VideoCodecType codec);
};

}  // namespace sora

#endif
