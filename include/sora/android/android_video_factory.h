#ifndef SORA_ANDROID_ANDROID_VIDEO_FACTORY_H_
#define SORA_ANDROID_ANDROID_VIDEO_FACTORY_H_

#include <memory>

#include "api/video_codecs/video_decoder_factory.h"
#include "api/video_codecs/video_encoder_factory.h"
#include "sdk/android/native_api/jni/jvm.h"

namespace sora {

std::unique_ptr<webrtc::VideoEncoderFactory> CreateAndroidVideoEncoderFactory(
    JNIEnv* env);
std::unique_ptr<webrtc::VideoDecoderFactory> CreateAndroidVideoDecoderFactory(
    JNIEnv* env);

}  // namespace sora

#endif
