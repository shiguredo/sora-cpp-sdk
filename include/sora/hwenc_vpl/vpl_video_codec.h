#ifndef SORA_HWENC_VPL_VPL_VIDEO_CODEC_H_
#define SORA_HWENC_VPL_VPL_VIDEO_CODEC_H_

#include "sora/sora_video_codec.h"
#include "sora/vpl_session.h"

namespace sora {

VideoCodecCapability::Engine GetVplVideoCodecCapability(
    std::shared_ptr<VplSession> session);

}  // namespace sora

#endif
