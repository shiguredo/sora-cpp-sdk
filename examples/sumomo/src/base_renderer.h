#ifndef BASE_RENDERER_H_
#define BASE_RENDERER_H_

#include <atomic>
#include <memory>
#include <thread>
#include <vector>

// WebRTC
#include <api/media_stream_interface.h>
#include <api/scoped_refptr.h>
#include <api/video/video_frame.h>
#include <api/video/video_sink_interface.h>
#include <rtc_base/synchronization/mutex.h>

class BaseRenderer {
 public:
  BaseRenderer(int width, int height, int fps);
  virtual ~BaseRenderer();

  void Start();
  void Stop();

  void SetSize(int width, int height);
  webrtc::Mutex* GetMutex();

  void AddTrack(webrtc::VideoTrackInterface* track);
  void RemoveTrack(webrtc::VideoTrackInterface* track);

  struct SinkInfo {
    int offset_x;
    int offset_y;
    // 入力フレームそのままのサイズ
    int input_width;
    int input_height;
    // イメージ領域のサイズ
    int frame_width;
    int frame_height;
    // 分割された領域のサイズ
    int width;
    int height;
  };
  virtual void RenderThreadStarted() = 0;
  virtual void RenderThreadFinished() = 0;
  virtual void Render(uint8_t* image,
                      int width,
                      int height,
                      const std::vector<SinkInfo>& sink_infos) = 0;

 private:
  class Sink : public webrtc::VideoSinkInterface<webrtc::VideoFrame> {
   public:
    Sink(BaseRenderer* renderer, webrtc::VideoTrackInterface* track);
    ~Sink();

    void OnFrame(const webrtc::VideoFrame& frame) override;

    void SetOutlineRect(int x, int y, int width, int height);

    webrtc::Mutex* GetMutex();
    bool GetOutlineChanged();
    int GetOffsetX();
    int GetOffsetY();
    int GetInputWidth();
    int GetInputHeight();
    int GetFrameWidth();
    int GetFrameHeight();
    int GetWidth();
    int GetHeight();
    uint8_t* GetImage();

   private:
    BaseRenderer* renderer_;
    webrtc::scoped_refptr<webrtc::VideoTrackInterface> track_;
    webrtc::Mutex frame_params_lock_;
    int outline_offset_x_;
    int outline_offset_y_;
    int outline_width_;
    int outline_height_;
    bool outline_changed_;
    float outline_aspect_;
    int input_width_;
    int input_height_;
    bool scaled_;
    std::unique_ptr<uint8_t[]> image_;
    int offset_x_;
    int offset_y_;
    int width_;
    int height_;
  };

 private:
  void RenderThread();
  void SetOutlines();

 private:
  webrtc::Mutex sinks_lock_;
  typedef std::vector<
      std::pair<webrtc::VideoTrackInterface*, std::unique_ptr<Sink>>>
      VideoTrackSinkVector;
  VideoTrackSinkVector sinks_;
  std::atomic<bool> running_;
  std::unique_ptr<std::thread> thread_;
  int width_;
  int height_;
  int fps_;
  int rows_;
  int cols_;
};

#endif
