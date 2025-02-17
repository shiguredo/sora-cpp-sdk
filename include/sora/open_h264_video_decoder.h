#ifndef SORA_OPEN_H264_VIDEO_DECODER_H_
#define SORA_OPEN_H264_VIDEO_DECODER_H_

#include <memory>
#include <string>

// WebRTC
#include <api/video_codecs/video_decoder.h>
#include <media/base/codec.h>

namespace sora {

std::unique_ptr<webrtc::VideoDecoder> CreateOpenH264VideoDecoder(
    const webrtc::SdpVideoFormat& format,
    std::string openh264);

}  // namespace sora

#endif
