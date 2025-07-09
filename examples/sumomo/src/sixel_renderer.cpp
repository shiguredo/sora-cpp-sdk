#include "sixel_renderer.h"

#include <algorithm>
#include <climits>
#include <cmath>
#include <csignal>
#include <iomanip>
#include <iostream>
#include <map>
#include <sstream>
#include <vector>

// WebRTC
#include <api/video/i420_buffer.h>
#include <libyuv/convert_from.h>
#include <libyuv/scale.h>
#include <libyuv/video_common.h>
#include <rtc_base/logging.h>

#define FRAME_INTERVAL (1000 / 10)  // 10 FPS for terminal display

SixelRenderer::SixelRenderer(int width, int height)
    : BaseRenderer(width, height, 10) {
  InitializeColorLookupTable();
  Start();
}

SixelRenderer::~SixelRenderer() {
  Stop();
}

void SixelRenderer::RenderThreadStarted() {}
void SixelRenderer::RenderThreadFinished() {}
void SixelRenderer::Render(uint8_t* image,
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
  OutputSixel(image, width, height);
}

void SixelRenderer::ClearScreen() {
  std::cout << "\033[2J\033[H" << std::flush;
}

void SixelRenderer::MoveCursorToTop() {
  std::cout << "\033[H" << std::flush;
}

void SixelRenderer::InitializeColorLookupTable() {
  // 固定の216色パレット（6x6x6 RGB）を作成
  palette_map_.clear();
  int color_index = 0;

  // 6段階のRGBの組み合わせで216色を作成
  for (int r = 0; r < 6; r++) {
    for (int g = 0; g < 6; g++) {
      for (int b = 0; b < 6; b++) {
        uint8_t red = (r * 255) / 5;
        uint8_t green = (g * 255) / 5;
        uint8_t blue = (b * 255) / 5;
        uint32_t color = (red << 16) | (green << 8) | blue;
        palette_map_[color] = color_index++;
      }
    }
  }

  // 残りの色でグレースケールを追加（216 + 40 = 256色）
  for (int gray = 0; gray < 40 && color_index < 256; gray++) {
    uint8_t value = (gray * 255) / 39;
    uint32_t color = (value << 16) | (value << 8) | value;
    // 既に存在する色は追加しない
    if (palette_map_.find(color) == palette_map_.end()) {
      palette_map_[color] = color_index++;
    }
  }

  // RGB値を5ビットに減らした時のルックアップテーブルを作成（32x32x32 = 32768エントリ）
  color_lookup_table_.resize(32 * 32 * 32);

  for (int r = 0; r < 32; r++) {
    for (int g = 0; g < 32; g++) {
      for (int b = 0; b < 32; b++) {
        // 5ビット値から8ビットに拡張（0-31 → 0-255）
        // 例: 31 (11111) を 255 (11111111) に変換
        // 左シフトで上位5ビットに配置し、右シフトで下位3ビットを埋める
        uint8_t r8 = (r << 3) | (r >> 2);
        uint8_t g8 = (g << 3) | (g >> 2);
        uint8_t b8 = (b << 3) | (b >> 2);

        // 最も近いパレット色を探す
        int min_distance = INT_MAX;
        int best_index = 0;

        for (const auto& [palette_color, palette_index] : palette_map_) {
          int pr = (palette_color >> 16) & 0xFF;
          int pg = (palette_color >> 8) & 0xFF;
          int pb = palette_color & 0xFF;

          int dr = r8 - pr;
          int dg = g8 - pg;
          int db = b8 - pb;

          int distance = dr * dr + dg * dg + db * db;

          if (distance < min_distance) {
            min_distance = distance;
            best_index = palette_index;
          }
        }

        // ルックアップテーブルに格納
        color_lookup_table_[(r << 10) | (g << 5) | b] = best_index;
      }
    }
  }
}

void SixelRenderer::OutputSixel(const uint8_t* rgb_data,
                                int width,
                                int height) {
  std::string output;
  output.reserve(width * height * 10);  // 大体のサイズを予約

  // 減色された画像データを作成（ルックアップテーブルを使用）
  std::vector<uint8_t> indexed_image(width * height);
  for (int y = 0; y < height; y++) {
    for (int x = 0; x < width; x++) {
      int pixel_offset = (y * width + x) * 4;
      uint8_t r = rgb_data[pixel_offset + 2];
      uint8_t g = rgb_data[pixel_offset + 1];
      uint8_t b = rgb_data[pixel_offset + 0];

      // RGB値を5ビットに減らしてルックアップテーブルのインデックスを作成
      int r5 = r >> 3;
      int g5 = g >> 3;
      int b5 = b >> 3;

      // ルックアップテーブルから色インデックスを取得
      int lookup_index = (r5 << 10) | (g5 << 5) | b5;
      indexed_image[y * width + x] = color_lookup_table_[lookup_index];
    }
  }

  // Sixelフォーマットでの出力開始
  output += "\033Pq\"1;1;";
  output += std::to_string(width);
  output += ";";
  output += std::to_string(height);

  // グローバルカラーパレットを定義
  for (const auto& [color, index] : palette_map_) {
    int r = (color >> 16) & 0xFF;
    int g = (color >> 8) & 0xFF;
    int b = color & 0xFF;

    // RGB値を0-100の範囲に変換
    int r_percent = (r * 100) / 255;
    int g_percent = (g * 100) / 255;
    int b_percent = (b * 100) / 255;

    output += "#";
    output += std::to_string(index);
    output += ";2;";
    output += std::to_string(r_percent);
    output += ";";
    output += std::to_string(g_percent);
    output += ";";
    output += std::to_string(b_percent);
  }

  // 6行ずつ処理
  for (int y = 0; y < height; y += 6) {
    // 各色について6行分のデータを出力
    bool first_color = true;
    for (const auto& [color, index] : palette_map_) {
      std::vector<uint8_t> sixel_line;
      bool has_pixels = false;

      // この色の行データを作成
      for (int x = 0; x < width; x++) {
        uint8_t sixel_byte = 0;

        for (int dy = 0; dy < 6 && y + dy < height; dy++) {
          int pixel_index = (y + dy) * width + x;
          if (indexed_image[pixel_index] == index) {
            sixel_byte |= (1 << dy);
            has_pixels = true;
          }
        }

        sixel_line.push_back(sixel_byte);
      }

      // この色にピクセルがある場合のみ出力
      if (has_pixels) {
        if (!first_color) {
          output += "$";  // 行の最初に戻る
        }
        first_color = false;

        output += "#";
        output += std::to_string(index);

        for (uint8_t sixel_byte : sixel_line) {
          // Sixel文字として出力（63を加える）
          output += static_cast<char>(sixel_byte + 63);
        }
      }
    }

    output += "-";  // 次の6行へ
  }

  // Sixel終了
  output += "\033\\";

  // 一括出力
  std::cout << output << std::flush;
}
