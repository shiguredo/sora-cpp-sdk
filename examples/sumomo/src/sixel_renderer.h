#ifndef SIXEL_RENDERER_H_
#define SIXEL_RENDERER_H_

#include <atomic>
#include <memory>
#include <string>
#include <vector>

// WebRTC
#include <api/media_stream_interface.h>
#include <api/scoped_refptr.h>
#include <api/video/video_frame.h>
#include <api/video/video_sink_interface.h>
#include <rtc_base/synchronization/mutex.h>

#include "base_renderer.h"

class SixelRenderer : public BaseRenderer {
 public:
  SixelRenderer(int width, int height);
  ~SixelRenderer();

  void RenderThreadStarted() override;
  void RenderThreadFinished() override;
  void Render(uint8_t* image,
              int width,
              int height,
              const std::vector<SinkInfo>& sink_infos) override;

 private:
  void OutputSixel(const uint8_t* rgb_data, int width, int height);
  void ClearScreen();
  void MoveCursorToTop();
  void InitializeColorLookupTable();

  // 色変換用ルックアップテーブル
  std::vector<uint8_t> color_lookup_table_;
  std::map<uint32_t, int> palette_map_;
};

#endif