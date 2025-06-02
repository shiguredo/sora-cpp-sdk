#ifndef FAKE_VIDEO_CAPTURER_H_
#define FAKE_VIDEO_CAPTURER_H_

// Sora C++ SDK
#include <sora/scalable_track_source.h>

struct FakeVideoCapturerConfig : sora::ScalableVideoTrackSourceConfig {
  int width;
  int height;
  int fps;
};

class FakeVideoCapturer : public sora::ScalableVideoTrackSource {
 public:
  using sora::ScalableVideoTrackSource::ScalableVideoTrackSource;
  virtual void StartCapture() = 0;
  virtual void StopCapture() = 0;
};

webrtc::scoped_refptr<FakeVideoCapturer> CreateFakeVideoCapturer(
    FakeVideoCapturerConfig config);

#endif