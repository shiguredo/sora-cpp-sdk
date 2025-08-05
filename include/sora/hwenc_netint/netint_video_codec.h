#ifndef SORA_HWENC_NETINT_NETINT_VIDEO_CODEC_H_
#define SORA_HWENC_NETINT_NETINT_VIDEO_CODEC_H_

#include <memory>

#include "sora/netint_context.h"
#include "sora/sora_video_codec.h"

namespace sora {

VideoCodecCapability::Engine GetNetintVideoCodecCapability(
    std::shared_ptr<NetintContext> context);

}  // namespace sora

#endif