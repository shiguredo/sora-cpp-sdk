#ifndef SORA_HWENC_MSDK_MSDK_VIDEO_ENCODER_H_
#define SORA_HWENC_MSDK_MSDK_VIDEO_ENCODER_H_

#include <memory>

// WebRTC
#include <api/video/video_codec_type.h>
#include <api/video_codecs/video_encoder.h>

#include "msdk_session.h"

namespace sora {

class MsdkVideoEncoder : public webrtc::VideoEncoder {
 public:
  static bool IsSupported(std::shared_ptr<MsdkSession> session,
                          webrtc::VideoCodecType codec);
  static std::unique_ptr<MsdkVideoEncoder> Create(
      std::shared_ptr<MsdkSession> session,
      webrtc::VideoCodecType codec);
};

}  // namespace sora

#endif
