#ifndef SORA_VPL_SESSION_IMPL_H_
#define SORA_VPL_SESSION_IMPL_H_

#include <vpl/mfxsession.h>
#include <memory>
#if defined(USE_VPL_ENCODER)

// Intel VPL

#include "sora/vpl_session.h"

namespace sora {

mfxSession GetVplSession(std::shared_ptr<VplSession> session);

}  // namespace sora

#endif

#endif