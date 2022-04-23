#ifndef CUDA_CONTEXT_CUDA_H_
#define CUDA_CONTEXT_CUDA_H_

#include <cuda.h>

#include "sora/cuda/cuda_context.h"

CUcontext GetCudaContext(std::shared_ptr<CudaContext> ctx);

#endif