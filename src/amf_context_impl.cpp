#include <chrono>
#include <memory>
#include <thread>

// WebRTC
#include <rtc_base/logging.h>

#include "sora/amf_context.h"

#if !defined(USE_AMF_ENCODER)

namespace sora {

std::shared_ptr<AMFContext> AMFContext::Create() {
  return nullptr;
}
bool AMFContext::CanCreate() {
  return false;
}

}  // namespace sora

#else

// AMF
#include <public/common/AMFFactory.h>
#include <public/include/core/Result.h>

#include "amf_context_impl.h"

namespace sora {

struct AMFContextImpl : AMFContext {
  ~AMFContextImpl() { g_AMFFactory.Terminate(); }
};

std::shared_ptr<AMFContext> AMFContext::Create() {
  const int kMaxRetries = 3;
  const int kRetryDelayMs = 100;

  for (int i = 0; i < kMaxRetries; ++i) {
    AMF_RESULT res = g_AMFFactory.Init();
    if (res == AMF_OK) {
      return std::make_shared<AMFContextImpl>();
    }

    if (i < kMaxRetries - 1) {
      std::this_thread::sleep_for(std::chrono::milliseconds(kRetryDelayMs));
    }
  }

  return nullptr;
}

bool AMFContext::CanCreate() {
  AMF_RESULT res = g_AMFFactory.Init();
  if (res != AMF_OK) {
    return false;
  }
  g_AMFFactory.Terminate();
  return true;
}

AMFFactoryHelper* GetAMFFactoryHelper(std::shared_ptr<AMFContext> ctx) {
  return &g_AMFFactory;
}

}  // namespace sora

#endif
