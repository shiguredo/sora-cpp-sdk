#ifndef SORA_HWENC_MSDK_MSDK_VIDEO_DECODER_H_
#define SORA_HWENC_MSDK_MSDK_VIDEO_DECODER_H_

#include <memory>

// WebRTC
#include <api/video/video_codec_type.h>
#include <api/video_codecs/video_decoder.h>

#include "msdk_session.h"

namespace sora {

class MsdkVideoDecoder : public webrtc::VideoDecoder {
 public:
  static bool IsSupported(std::shared_ptr<MsdkSession> session,
                          webrtc::VideoCodecType codec);
  static std::unique_ptr<MsdkVideoDecoder> Create(
      std::shared_ptr<MsdkSession> session,
      webrtc::VideoCodecType codec);
};

}  // namespace sora

#endif
