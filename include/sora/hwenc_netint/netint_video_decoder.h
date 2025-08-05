#ifndef SORA_HWENC_NETINT_NETINT_VIDEO_DECODER_H_
#define SORA_HWENC_NETINT_NETINT_VIDEO_DECODER_H_

#include <memory>

// WebRTC
#include <api/video/video_codec_type.h>
#include <api/video_codecs/video_decoder.h>

#include "sora/netint_context.h"

namespace sora {

class NetintVideoDecoder : public webrtc::VideoDecoder {
 public:
  static bool IsSupported(std::shared_ptr<NetintContext> context,
                          webrtc::VideoCodecType codec);
  static std::unique_ptr<NetintVideoDecoder> Create(
      std::shared_ptr<NetintContext> context,
      webrtc::VideoCodecType codec);
};

}  // namespace sora

#endif