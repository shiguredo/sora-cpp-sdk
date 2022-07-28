#include "sora/hwenc_vpl/vpl_session.h"

#include <iostream>

// oneVPL
#include <vpl/mfxdispatcher.h>
#include <vpl/mfxvideo.h>

#ifdef __linux__
#include "vaapi_utils_drm.h"
#endif

namespace sora {

struct VplSessionImpl : VplSession {
  ~VplSessionImpl();

  mfxLoader loader = nullptr;
  mfxSession session = nullptr;

#ifdef __linux__
  std::unique_ptr<DRMLibVA> libva;
#endif
};

VplSessionImpl::~VplSessionImpl() {
  MFXClose(session);
  MFXUnload(loader);
}

std::shared_ptr<VplSession> VplSession::Create() {
  std::shared_ptr<VplSessionImpl> session(new VplSessionImpl());

  mfxStatus sts = MFX_ERR_NONE;

  session->loader = MFXLoad();
  if (session->loader == nullptr) {
    std::cerr << "Failed to MFXLoad" << std::endl;
    return nullptr;
  }

  MFX_ADD_PROPERTY_U32(session->loader, "mfxImplDescription.Impl",
                       MFX_IMPL_TYPE_HARDWARE);

  sts = MFXCreateSession(session->loader, 0, &session->session);
  if (sts != MFX_ERR_NONE) {
    // std::cerr << "Failed to MFXCreateSession: sts=" << sts << std::endl;
    return nullptr;
  }

#ifdef __linux__
  session->libva = CreateDRMLibVA();
  if (!session->libva) {
    return nullptr;
  }

  sts = MFXVideoCORE_SetHandle(
      session->session, static_cast<mfxHandleType>(MFX_HANDLE_VA_DISPLAY),
      session->libva->GetVADisplay());
  if (sts != MFX_ERR_NONE) {
    return nullptr;
  }
#endif

  // Query selected implementation and version
  mfxIMPL impl;
  sts = MFXQueryIMPL(session->session, &impl);
  if (sts != MFX_ERR_NONE) {
    std::cerr << "Failed to MFXQueryIMPL: sts=" << sts << std::endl;
    return nullptr;
  }

  mfxVersion ver;
  sts = MFXQueryVersion(session->session, &ver);
  if (sts != MFX_ERR_NONE) {
    std::cerr << "Failed to MFXQueryVersion: sts=" << sts << std::endl;
    return nullptr;
  }

  // std::cout << "oneVPL Implementation: "
  //           << (impl == MFX_IMPL_SOFTWARE ? "SOFTWARE" : "HARDWARE")
  //           << std::endl;
  // std::cout << "oneVPL Version: " << ver.Major << "." << ver.Minor << std::endl;
  return session;
}

mfxSession GetVplSession(std::shared_ptr<VplSession> session) {
  return std::static_pointer_cast<VplSessionImpl>(session)->session;
}

}  // namespace sora
