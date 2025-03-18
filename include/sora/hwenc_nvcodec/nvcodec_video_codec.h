#ifndef SORA_HWENC_NVCODEC_NVCODEC_VIDEO_CODEC_H_
#define SORA_HWENC_NVCODEC_NVCODEC_VIDEO_CODEC_H_

#include <memory>

#include "sora/cuda_context.h"
#include "sora/sora_video_codec.h"

namespace sora {

VideoCodecCapability::Engine GetNvCodecVideoCodecCapability(
    std::shared_ptr<CudaContext> context);

}  // namespace sora

#endif
