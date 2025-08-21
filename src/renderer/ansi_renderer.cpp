#include "sora/renderer/ansi_renderer.h"

#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

#include "sora/renderer/base_renderer.h"

namespace sora {

AnsiRenderer::AnsiRenderer(int width, int height)
    : BaseRenderer(width, height, 10) {
  Start();
}

AnsiRenderer::~AnsiRenderer() {
  Stop();
}

void AnsiRenderer::RenderThreadStarted() {}
void AnsiRenderer::RenderThreadFinished() {}

void AnsiRenderer::Render(uint8_t* image,
                          int width,
                          int height,
                          const std::vector<SinkInfo>& sink_infos) {
  MoveCursorToTop();
  // オリジナルサイズを表示
  for (const auto& sink_info : sink_infos) {
    std::cout << "\033[2K" << "(" << sink_info.offset_x << ","
              << sink_info.offset_y << ")"
              << " の元のサイズ: " << sink_info.input_width << "x"
              << sink_info.input_height << std::endl;
  }
  OutputAnsi(image, width, height);
}

void AnsiRenderer::ClearScreen() {
  std::cout << "\033[2J\033[H" << std::flush;
}

void AnsiRenderer::MoveCursorToTop() {
  std::cout << "\033[H" << std::flush;
}

int AnsiRenderer::RgbToAnsi256(uint8_t r, uint8_t g, uint8_t b) {
  // 216色キューブ（6x6x6）を使用
  // RGB値を0-5の範囲に変換
  int r6 = (r * 5) / 255;
  int g6 = (g * 5) / 255;
  int b6 = (b * 5) / 255;

  // ANSI 256色の216色キューブは16から始まる
  return 16 + (r6 * 36) + (g6 * 6) + b6;
}

std::string AnsiRenderer::RgbToAnsi(uint8_t r, uint8_t g, uint8_t b) {
  // ANSI 256色を使用してより正確な色表現
  int color_code = RgbToAnsi256(r, g, b);
  return "\033[48;5;" + std::to_string(color_code) + "m";
}

void AnsiRenderer::OutputAnsi(const uint8_t* rgb_data, int width, int height) {
  std::string output;
  output.reserve(width * height * 20);  // 大体のサイズを予約

  // 2x1ピクセルを1文字で表現（上半分と下半分の色を使用）
  for (int y = 0; y < height; y += 2) {
    output += "\033[2K";  // 行をクリア

    for (int x = 0; x < width; x++) {
      // 上のピクセル（y）
      int upper_offset = (y * width + x) * 4;
      uint8_t upper_r = rgb_data[upper_offset + 2];
      uint8_t upper_g = rgb_data[upper_offset + 1];
      uint8_t upper_b = rgb_data[upper_offset + 0];

      // 下のピクセル（y+1）
      uint8_t lower_r = upper_r, lower_g = upper_g, lower_b = upper_b;
      if (y + 1 < height) {
        int lower_offset = ((y + 1) * width + x) * 4;
        lower_r = rgb_data[lower_offset + 2];
        lower_g = rgb_data[lower_offset + 1];
        lower_b = rgb_data[lower_offset + 0];
      }

      // 上半分の色を前景色、下半分の色を背景色として設定
      int upper_color = RgbToAnsi256(upper_r, upper_g, upper_b);
      int lower_color = RgbToAnsi256(lower_r, lower_g, lower_b);

      // 上半分ブロック文字（▀）を使用
      output += "\033[38;5;";
      output += std::to_string(upper_color);
      output += "m\033[48;5;";
      output += std::to_string(lower_color);
      output += "m▀";
    }

    output += "\033[0m\n";  // 色をリセットして改行
  }

  // 一括出力
  std::cout << output << std::flush;
}

}  // namespace sora
