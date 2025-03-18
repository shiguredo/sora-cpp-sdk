#ifndef SORA_OPEN_H264_VIDEO_CODEC_H_
#define SORA_OPEN_H264_VIDEO_CODEC_H_

#include <memory>
#include <optional>
#include <string>

#include "sora_video_codec.h"

namespace sora {

VideoCodecCapability::Engine GetOpenH264VideoCodecCapability(
    std::optional<std::string> openh264_path);

}  // namespace sora

#endif
