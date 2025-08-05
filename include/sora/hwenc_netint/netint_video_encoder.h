#ifndef SORA_HWENC_NETINT_NETINT_VIDEO_ENCODER_H_
#define SORA_HWENC_NETINT_NETINT_VIDEO_ENCODER_H_

#include <memory>

// WebRTC
#include <api/video/video_codec_type.h>
#include <api/video_codecs/video_encoder.h>

#include "sora/netint_context.h"

namespace sora {

class NetintVideoEncoder : public webrtc::VideoEncoder {
 public:
  static bool IsSupported(std::shared_ptr<NetintContext> context,
                          webrtc::VideoCodecType codec);
  static std::unique_ptr<NetintVideoEncoder> Create(
      std::shared_ptr<NetintContext> context,
      webrtc::VideoCodecType codec);
};

}  // namespace sora

#endif