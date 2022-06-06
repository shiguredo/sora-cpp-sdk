#ifndef SORA_HWENC_MSDK_MSDK_SESSION_H_
#define SORA_HWENC_MSDK_MSDK_SESSION_H_

#include <memory>

namespace sora {

struct MsdkSession {
  static std::shared_ptr<MsdkSession> Create();
};

}  // namespace sora

#endif
