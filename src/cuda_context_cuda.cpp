#include "sora/fix_cuda_noinline_macro_error.h"

#include "sora/cuda_context.h"

#if !defined(USE_NVCODEC_ENCODER)

namespace sora {

std::shared_ptr<CudaContext> CudaContext::Create() {
  return nullptr;
}

void* CudaContext::Context() const {
  return nullptr;
}

}  // namespace sora

#else

#include "cuda_context_cuda.h"

// NvCodec
#include <NvDecoder/NvDecoder.h>

#include "sora/dyn/cuda.h"

// どこかにグローバルな logger の定義が必要
simplelogger::Logger* logger =
    simplelogger::LoggerFactory::CreateConsoleLogger();

namespace sora {

struct CudaContextImpl : CudaContext {
  CUdevice device;
  CUcontext context;
  ~CudaContextImpl() { dyn::cuCtxDestroy(context); }
};

#define ckerror(call) \
  if (!ck(call))      \
  throw std::exception()

std::shared_ptr<CudaContext> CudaContext::Create() {
  try {
    CUdevice device;
    CUcontext context;

    if (!dyn::DynModule::Instance().IsLoadable(dyn::CUDA_SO)) {
      throw std::exception();
    }

    ckerror(dyn::cuInit(0));
    ckerror(dyn::cuDeviceGet(&device, 0));
    char device_name[80];
    ckerror(dyn::cuDeviceGetName(device_name, sizeof(device_name), device));
    //RTC_LOG(LS_INFO) << "GPU in use: " << device_name;
    ckerror(dyn::cuCtxCreate(&context, 0, device));

    auto impl = std::make_shared<CudaContextImpl>();
    impl->device = device;
    impl->context = context;
    return impl;
  } catch (std::exception&) {
    return nullptr;
  }
}

CUdevice GetCudaDevice(std::shared_ptr<CudaContext> ctx) {
  return std::static_pointer_cast<CudaContextImpl>(ctx)->device;
}

CUcontext GetCudaContext(std::shared_ptr<CudaContext> ctx) {
  return std::static_pointer_cast<CudaContextImpl>(ctx)->context;
}

}  // namespace sora

#endif
