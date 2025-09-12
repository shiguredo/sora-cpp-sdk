#include "sora/hwenc_vpl/vpl_video_codec.h"
#include <memory>
#include <string>

// WebRTC
#include <api/video/video_codec_type.h>

// VPL
#include <vpl/mfxcommon.h>
#include <vpl/mfxdefs.h>
#include <vpl/mfxsession.h>

#include "../vpl_session_impl.h"
#include "sora/hwenc_vpl/vpl_video_decoder.h"
#include "sora/hwenc_vpl/vpl_video_encoder.h"
#include "sora/sora_video_codec.h"
#include "sora/vpl_session.h"

namespace sora {

VideoCodecCapability::Engine GetVplVideoCodecCapability(
    std::shared_ptr<VplSession> session) {
  VideoCodecCapability::Engine engine(VideoCodecImplementation::kIntelVpl);
  if (session == nullptr) {
    return engine;
  }

  mfxStatus sts = MFX_ERR_NONE;
  mfxIMPL impl;
  sts = MFXQueryIMPL(GetVplSession(session), &impl);
  if (sts != MFX_ERR_NONE) {
    return engine;
  }

  mfxVersion ver;
  sts = MFXQueryVersion(GetVplSession(session), &ver);
  if (sts != MFX_ERR_NONE) {
    return engine;
  }

  engine.parameters.version =
      std::to_string(ver.Major) + "." + std::to_string(ver.Minor);
  engine.parameters.vpl_impl =
      impl == MFX_IMPL_SOFTWARE ? "SOFTWARE" : "HARDWARE";
  engine.parameters.vpl_impl_value = (int)impl;

  auto add = [&engine, &session](webrtc::VideoCodecType type) {
    engine.codecs.emplace_back(type,
                               VplVideoEncoder::IsSupported(session, type),
                               VplVideoDecoder::IsSupported(session, type));
  };
  add(webrtc::kVideoCodecVP8);
  add(webrtc::kVideoCodecVP9);
  add(webrtc::kVideoCodecH264);
  add(webrtc::kVideoCodecH265);
  add(webrtc::kVideoCodecAV1);
  return engine;
}

}  // namespace sora
