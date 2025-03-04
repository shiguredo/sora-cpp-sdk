#include "sora/hwenc_amf/amf_video_codec.h"

#include "sora/hwenc_amf/amf_video_decoder.h"
#include "sora/hwenc_amf/amf_video_encoder.h"

#include "../amf_context_impl.h"

namespace sora {

VideoCodecCapability::Engine GetAMFVideoCodecCapability(
    std::shared_ptr<AMFContext> context) {
  VideoCodecCapability::Engine engine(VideoCodecImplementation::kAmdAmf);
  engine.parameters.amf_runtime_version =
      GetAMFFactoryHelper(context)->AMFQueryVersion();

  auto add = [&engine, &context](webrtc::VideoCodecType type) {
    engine.codecs.emplace_back(type,
                               AMFVideoEncoder::IsSupported(context, type),
                               AMFVideoDecoder::IsSupported(context, type));
  };
  add(webrtc::kVideoCodecVP8);
  add(webrtc::kVideoCodecVP9);
  add(webrtc::kVideoCodecH264);
  add(webrtc::kVideoCodecH265);
  add(webrtc::kVideoCodecAV1);
  return engine;
}

}  // namespace sora
