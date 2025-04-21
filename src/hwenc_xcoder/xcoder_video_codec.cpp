#include "sora/hwenc_xcoder/xcoder_video_codec.h"

#include "sora/hwenc_xcoder/xcoder_video_decoder.h"
#include "sora/hwenc_xcoder/xcoder_video_encoder.h"

namespace sora {

VideoCodecCapability::Engine GetLibxcoderVideoCodecCapability() {
  VideoCodecCapability::Engine engine(
      VideoCodecImplementation::kNetintLibxcoder);

  // engine.parameters.version =
  //     std::to_string(ver.Major) + "." + std::to_string(ver.Minor);

  auto add = [&engine](webrtc::VideoCodecType type) {
    engine.codecs.emplace_back(type, LibxcoderVideoEncoder::IsSupported(type),
                               LibxcoderVideoDecoder::IsSupported(type));
  };
  add(webrtc::kVideoCodecVP8);
  add(webrtc::kVideoCodecVP9);
  add(webrtc::kVideoCodecH264);
  add(webrtc::kVideoCodecH265);
  add(webrtc::kVideoCodecAV1);
  return engine;
}

}  // namespace sora
