#include "default_video_formats.h"

#include <vector>

// WebRTC
#include <api/rtp_parameters.h>
#include <api/video/video_codec_type.h>
#include <api/video_codecs/h264_profile_level_id.h>
#include <api/video_codecs/sdp_video_format.h>
#include <media/base/media_constants.h>
#include <modules/video_coding/codecs/av1/av1_svc_config.h>
#include <modules/video_coding/codecs/h264/include/h264.h>
#include <modules/video_coding/codecs/vp9/include/vp9.h>

namespace sora {

std::vector<webrtc::SdpVideoFormat> GetDefaultVideoFormats(
    webrtc::VideoCodecType codec) {
  std::vector<webrtc::SdpVideoFormat> r;
  if (codec == webrtc::kVideoCodecVP8) {
    r.push_back(webrtc::SdpVideoFormat(webrtc::kVp8CodecName));
  } else if (codec == webrtc::kVideoCodecVP9) {
    for (const webrtc::SdpVideoFormat& format :
         webrtc::SupportedVP9Codecs(true)) {
      r.push_back(format);
    }
  } else if (codec == webrtc::kVideoCodecAV1) {
    r.push_back(webrtc::SdpVideoFormat(
        webrtc::kAv1CodecName, webrtc::CodecParameterMap(),
        webrtc::LibaomAv1EncoderSupportedScalabilityModes()));
  } else if (codec == webrtc::kVideoCodecH264) {
    r.push_back(CreateH264Format(webrtc::H264Profile::kProfileBaseline,
                                 webrtc::H264Level::kLevel3_1, "1", true));
    r.push_back(CreateH264Format(webrtc::H264Profile::kProfileBaseline,
                                 webrtc::H264Level::kLevel3_1, "0", true));
    r.push_back(
        CreateH264Format(webrtc::H264Profile::kProfileConstrainedBaseline,
                         webrtc::H264Level::kLevel3_1, "1", true));
    r.push_back(
        CreateH264Format(webrtc::H264Profile::kProfileConstrainedBaseline,
                         webrtc::H264Level::kLevel3_1, "0", true));
  } else if (codec == webrtc::kVideoCodecH265) {
    r.push_back(webrtc::SdpVideoFormat(webrtc::kH265CodecName));
  }
  return r;
}

}  // namespace sora
