#include "nvcodec_video_codec_cuda.h"

#include "../cuda_context_cuda.h"
#include "sora/dyn/cuda.h"

namespace sora {

void GetNvCodecGpuDeviceName(std::shared_ptr<CudaContext> context,
                             char* name,
                             size_t size) {
  if (context == nullptr) {
    return;
  }

  CUresult r = dyn::cuDeviceGetName(name, size, GetCudaDevice(context));
  if (r != CUDA_SUCCESS) {
    // const char* error = NULL;
    // dyn::cuGetErrorName(r, &error);
    // std::cerr << "Failed to cuDeviceGetName: error=" << error << std::endl;
  }
}

}  // namespace sora
