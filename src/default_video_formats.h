#ifndef SORA_DEFAULT_VIDEO_FORMATS_H_
#define SORA_DEFAULT_VIDEO_FORMATS_H_

#include <optional>
#include <vector>

// WebRTC
#include <api/video/video_codec_type.h>
#include <api/video_codecs/sdp_video_format.h>

namespace sora {

// 前方宣言
enum class HevcHardwareType;

std::vector<webrtc::SdpVideoFormat> GetDefaultVideoFormats(
    webrtc::VideoCodecType codec);

// ハードウェアタイプを指定できるオーバーロード版
std::vector<webrtc::SdpVideoFormat> GetDefaultVideoFormats(
    webrtc::VideoCodecType codec,
    HevcHardwareType hw_type,
    std::optional<int> max_hevc_level = std::nullopt);

}  // namespace sora

#endif