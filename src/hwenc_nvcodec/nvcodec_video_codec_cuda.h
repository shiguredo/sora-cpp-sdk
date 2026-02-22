#ifndef SORA_HWENC_NVCODEC_NVCODEC_VIDEO_CODEC_CUDA_H_
#define SORA_HWENC_NVCODEC_NVCODEC_VIDEO_CODEC_CUDA_H_

#include <cstddef>
#include <memory>

#include "sora/cuda_context.h"

namespace sora {

void GetNvCodecGpuDeviceName(std::shared_ptr<CudaContext> context,
                             char* name,
                             size_t size);

}  // namespace sora

#endif
