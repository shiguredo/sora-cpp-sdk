#include <iostream>

// Intel Media SDK
#include <mfx/mfxvideo++.h>

#include "sora/hwenc_msdk/msdk_session.h"

namespace sora {

mfxSession GetMsdkSession(std::shared_ptr<MsdkSession> session);

}  // namespace sora
