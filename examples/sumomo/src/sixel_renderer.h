#ifndef SIXEL_RENDERER_H_
#define SIXEL_RENDERER_H_

#include <memory>
#include <string>
#include <vector>
#include <thread>
#include <atomic>
#include <condition_variable>

// Boost
#include <boost/asio.hpp>

// WebRTC
#include <api/media_stream_interface.h>
#include <api/scoped_refptr.h>
#include <api/video/video_frame.h>
#include <api/video/video_sink_interface.h>
#include <rtc_base/synchronization/mutex.h>

class SixelRenderer {
 public:
  SixelRenderer(int width, int height, bool clear_screen = true);
  ~SixelRenderer();

  void SetDispatchFunction(std::function<void(std::function<void()>)> dispatch);

  void RenderThread();

  void AddTrack(webrtc::VideoTrackInterface* track);
  void RemoveTrack(webrtc::VideoTrackInterface* track);

 protected:
  class Sink : public webrtc::VideoSinkInterface<webrtc::VideoFrame> {
   public:
    Sink(SixelRenderer* renderer, webrtc::VideoTrackInterface* track);
    ~Sink();

    void OnFrame(const webrtc::VideoFrame& frame) override;

    webrtc::Mutex* GetMutex();
    bool HasNewFrame();
    int GetFrameWidth();
    int GetFrameHeight();
    int GetOriginalWidth();
    int GetOriginalHeight();
    uint8_t* GetImage();
    void ResetNewFrame();

   private:
    SixelRenderer* renderer_;
    webrtc::scoped_refptr<webrtc::VideoTrackInterface> track_;
    webrtc::Mutex frame_params_lock_;
    int input_width_;
    int input_height_;
    int original_width_;
    int original_height_;
    int scaled_width_;
    int scaled_height_;
    std::unique_ptr<uint8_t[]> image_;
    bool has_new_frame_;
  };

 private:
  void OutputSixel(const uint8_t* rgb_data, int width, int height);
  void ClearScreen();
  void MoveCursorToTop();
  void InitializeColorLookupTable();

  webrtc::Mutex sinks_lock_;
  typedef std::vector<
      std::pair<webrtc::VideoTrackInterface*, std::unique_ptr<Sink>>>
      VideoTrackSinkVector;
  VideoTrackSinkVector sinks_;
  std::atomic<bool> running_;
  std::unique_ptr<std::thread> thread_;
  std::function<void(std::function<void()>)> dispatch_;
  int width_;
  int height_;
  bool clear_screen_;
  webrtc::Mutex render_lock_;
  std::condition_variable render_cv_;
  
  // 色変換用ルックアップテーブル
  std::vector<uint8_t> color_lookup_table_;
  std::map<uint32_t, int> palette_map_;
};

#endif