#ifndef FAKE_VIDEO_CAPTURER_H_INCLUDED
#define FAKE_VIDEO_CAPTURER_H_INCLUDED

#include <functional>

// WebRTC
#include <api/scoped_refptr.h>

// Sora C++ SDK
#include <sora/scalable_track_source.h>

namespace sora {

struct FakeVideoCapturerConfig : ScalableVideoTrackSourceConfig {
  int width = 640;
  int height = 480;
  int fps = 30;
  // 円が一周した時に呼ばれるコールバック
  std::function<void()> on_tick;
};

class FakeVideoCapturer : public ScalableVideoTrackSource {
 public:
  using ScalableVideoTrackSource::ScalableVideoTrackSource;
  static webrtc::scoped_refptr<FakeVideoCapturer> Create(
      FakeVideoCapturerConfig config);
  virtual void StartCapture() = 0;
  virtual void StopCapture() = 0;
};

}  // namespace sora

#endif
