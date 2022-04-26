#ifndef SORA_DEFAULT_VIDEO_FORMATS_H_
#define SORA_DEFAULT_VIDEO_FORMATS_H_

#include <vector>

// WebRTC
#include <api/video/video_codec_type.h>
#include <api/video_codecs/sdp_video_format.h>

namespace sora {

std::vector<webrtc::SdpVideoFormat> GetDefaultVideoFormats(
    webrtc::VideoCodecType codec);

}

#endif