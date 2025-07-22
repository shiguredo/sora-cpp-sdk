#include "sora/open_h264_video_codec.h"

#include <optional>
#include <string>

#if defined(_WIN32)
// Windows
#include <windows.h>
#else
// Linux
#include <dlfcn.h>
#endif

// WebRTC
#include <api/video/video_codec_type.h>

// OpenH264
#include <wels/codec_app_def.h>

#include "sora/sora_video_codec.h"

namespace sora {

VideoCodecCapability::Engine GetOpenH264VideoCodecCapability(
    std::optional<std::string> openh264_path) {
  VideoCodecCapability::Engine engine(VideoCodecImplementation::kCiscoOpenH264);
  engine.codecs.emplace_back(webrtc::kVideoCodecVP8, false, false);
  engine.codecs.emplace_back(webrtc::kVideoCodecVP9, false, false);
  engine.codecs.emplace_back(webrtc::kVideoCodecH265, false, false);
  engine.codecs.emplace_back(webrtc::kVideoCodecAV1, false, false);
  auto& h264 =
      engine.codecs.emplace_back(webrtc::kVideoCodecH264, false, false);
  if (!openh264_path) {
    return engine;
  }
#if defined(_WIN32)
  HMODULE handle = LoadLibraryA(openh264_path->c_str());
#else
  void* handle = ::dlopen(openh264_path->c_str(), RTLD_LAZY);
#endif

  if (handle == nullptr) {
    return engine;
  }
  typedef void (*WelsGetCodecVersionExFunc)(OpenH264Version* pVersion);
#if defined(_WIN32)
  auto f = (WelsGetCodecVersionExFunc)::GetProcAddress(handle,
                                                       "WelsGetCodecVersionEx");
  if (f == nullptr) {
    FreeLibrary(handle);
    return engine;
  }
#else
  auto f = (WelsGetCodecVersionExFunc)::dlsym(handle, "WelsGetCodecVersionEx");
  if (f == nullptr) {
    ::dlclose(handle);
    return engine;
  }
#endif

  OpenH264Version version;
  f(&version);
  h264.encoder = true;
  h264.decoder = true;
  h264.parameters.openh264_path = openh264_path;
  h264.parameters.version = std::to_string(version.uMajor) + "." +
                            std::to_string(version.uMinor) + "." +
                            std::to_string(version.uRevision);

#if defined(_WIN32)
  FreeLibrary(handle);
#else
  ::dlclose(handle);
#endif

  return engine;
}

}  // namespace sora
