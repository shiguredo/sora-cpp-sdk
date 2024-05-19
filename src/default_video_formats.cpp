#include "default_video_formats.h"

// WebRTC
#include <media/base/media_constants.h>
#include <api/video_codecs/sdp_video_format.h>
#include <api/video_codecs/video_codec.h>
#include <api/video_codecs/vp9_profile.h>
#include <modules/video_coding/codecs/av1/av1_svc_config.h>
#include <modules/video_coding/codecs/h264/include/h264.h>
#include <modules/video_coding/codecs/vp8/include/vp8.h>
#include <modules/video_coding/codecs/vp9/include/vp9.h>

namespace sora {

std::vector<webrtc::SdpVideoFormat> GetDefaultVideoFormats(
    webrtc::VideoCodecType codec) {
  std::vector<webrtc::SdpVideoFormat> r;
  if (codec == webrtc::kVideoCodecVP8) {
    r.push_back(webrtc::SdpVideoFormat(cricket::kVp8CodecName));
  } else if (codec == webrtc::kVideoCodecVP9) {
    for (const webrtc::SdpVideoFormat& format :
         webrtc::SupportedVP9Codecs(true)) {
      r.push_back(format);
    }
  } else if (codec == webrtc::kVideoCodecAV1) {
    r.push_back(webrtc::SdpVideoFormat(
        cricket::kAv1CodecName, webrtc::SdpVideoFormat::Parameters(),
        webrtc::LibaomAv1EncoderSupportedScalabilityModes()));
  } else if (codec == webrtc::kVideoCodecH264) {
    r.push_back(CreateH264Format(webrtc::H264Profile::kProfileBaseline,
                                 webrtc::H264Level::kLevel3_1, "1"));
    r.push_back(CreateH264Format(webrtc::H264Profile::kProfileBaseline,
                                 webrtc::H264Level::kLevel3_1, "0"));
    r.push_back(
        CreateH264Format(webrtc::H264Profile::kProfileConstrainedBaseline,
                         webrtc::H264Level::kLevel3_1, "1"));
    r.push_back(
        CreateH264Format(webrtc::H264Profile::kProfileConstrainedBaseline,
                         webrtc::H264Level::kLevel3_1, "0"));
  } else if (codec == webrtc::kVideoCodecH265) {
    r.push_back(webrtc::SdpVideoFormat(cricket::kH265CodecName));
  }
  return r;
}

}  // namespace sora
