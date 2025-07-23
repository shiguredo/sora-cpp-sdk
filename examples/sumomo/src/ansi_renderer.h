#ifndef ANSI_RENDERER_H_
#define ANSI_RENDERER_H_

#include <cstdint>
#include <string>
#include <vector>

#include "base_renderer.h"

class AnsiRenderer : public BaseRenderer {
 public:
  AnsiRenderer(int width, int height);
  ~AnsiRenderer() override;

  void RenderThreadStarted() override;
  void RenderThreadFinished() override;
  void Render(uint8_t* image,
              int width,
              int height,
              const std::vector<SinkInfo>& sink_infos) override;

 private:
  void OutputAnsi(const uint8_t* rgb_data, int width, int height);
  void ClearScreen();
  void MoveCursorToTop();
  std::string RgbToAnsi(uint8_t r, uint8_t g, uint8_t b);

  // ANSI 256色パレットへの変換用
  int RgbToAnsi256(uint8_t r, uint8_t g, uint8_t b);
};

#endif