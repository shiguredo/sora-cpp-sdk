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

SixelRenderer::SixelRenderer(int width, int height, bool clear_screen)
    : running_(true),
      dispatch_(nullptr),
      width_(width),
      height_(height),
      clear_screen_(clear_screen) {
  InitializeColorLookupTable();

  if (clear_screen_) {
    ClearScreen();
  }

  thread_ = std::make_unique<std::thread>([this]() { RenderThread(); });
}

SixelRenderer::~SixelRenderer() {
  running_ = false;
  render_cv_.notify_all();
  if (thread_ && thread_->joinable()) {
    thread_->join();
  }
  if (clear_screen_) {
    ClearScreen();
  }
}

void SixelRenderer::SetDispatchFunction(
    std::function<void(std::function<void()>)> dispatch) {
  webrtc::MutexLock lock(&sinks_lock_);
  dispatch_ = std::move(dispatch);
}

void SixelRenderer::RenderThread() {
  RTC_LOG(LS_INFO) << "Sixelレンダラースレッドを開始しました";

  auto start_time = std::chrono::steady_clock::now();

  while (running_) {
    auto frame_start = std::chrono::steady_clock::now();

    {
      webrtc::MutexLock lock(&sinks_lock_);

      // 最初のトラックからフレームを取得して表示
      if (!sinks_.empty()) {
        Sink* sink = sinks_[0].second.get();
        webrtc::MutexLock frame_lock(sink->GetMutex());

        if (sink->HasNewFrame()) {
          int width = sink->GetFrameWidth();
          int height = sink->GetFrameHeight();
          int original_width = sink->GetOriginalWidth();
          int original_height = sink->GetOriginalHeight();

          if (width > 0 && height > 0) {
            uint8_t* image = sink->GetImage();
            if (image) {
              MoveCursorToTop();
              // オリジナルサイズを表示（行をクリア）
              std::cout << "\033[2K元のサイズ: " << original_width << " x "
                        << original_height << std::endl;
              OutputSixel(image, width, height);
              sink->ResetNewFrame();
            }
          }
        }
      }
    }

    // フレームレート制御
    auto frame_end = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                       frame_end - frame_start)
                       .count();
    if (elapsed < FRAME_INTERVAL) {
      std::this_thread::sleep_for(
          std::chrono::milliseconds(FRAME_INTERVAL - elapsed));
    }
  }

  RTC_LOG(LS_INFO) << "Sixelレンダラースレッドを終了しました";
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
  std::cout << "\033Pq\"1;1;" << width << ";" << height;

  // グローバルカラーパレットを定義
  for (const auto& [color, index] : palette_map_) {
    int r = (color >> 16) & 0xFF;
    int g = (color >> 8) & 0xFF;
    int b = color & 0xFF;

    // RGB値を0-100の範囲に変換
    int r_percent = (r * 100) / 255;
    int g_percent = (g * 100) / 255;
    int b_percent = (b * 100) / 255;

    std::cout << "#" << index << ";2;" << r_percent << ";" << g_percent << ";"
              << b_percent;
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
          std::cout << "$";  // 行の最初に戻る
        }
        first_color = false;

        std::cout << "#" << index;

        for (uint8_t sixel_byte : sixel_line) {
          // Sixel文字として出力（63を加える）
          std::cout << static_cast<char>(sixel_byte + 63);
        }
      }
    }

    std::cout << "-";  // 次の6行へ
  }

  // Sixel終了
  std::cout << "\033\\" << std::flush;
}

SixelRenderer::Sink::Sink(SixelRenderer* renderer,
                          webrtc::VideoTrackInterface* track)
    : renderer_(renderer),
      track_(track),
      input_width_(0),
      input_height_(0),
      original_width_(0),
      original_height_(0),
      scaled_width_(0),
      scaled_height_(0),
      has_new_frame_(false) {
  track_->AddOrUpdateSink(this, webrtc::VideoSinkWants());
}

SixelRenderer::Sink::~Sink() {
  track_->RemoveSink(this);
}

void SixelRenderer::Sink::OnFrame(const webrtc::VideoFrame& frame) {
  if (frame.width() == 0 || frame.height() == 0)
    return;

  webrtc::MutexLock lock(&frame_params_lock_);

  // オリジナルサイズを記録
  original_width_ = frame.width();
  original_height_ = frame.height();

  // アスペクト比を保ってwidth_, height_にスケーリング
  float original_aspect =
      static_cast<float>(frame.width()) / static_cast<float>(frame.height());
  float target_aspect = static_cast<float>(renderer_->width_) /
                        static_cast<float>(renderer_->height_);

  if (original_aspect > target_aspect) {
    // 横長の場合、幅を基準にスケーリング
    scaled_width_ = renderer_->width_;
    scaled_height_ = static_cast<int>(renderer_->width_ / original_aspect);
  } else {
    // 縦長の場合、高さを基準にスケーリング
    scaled_height_ = renderer_->height_;
    scaled_width_ = static_cast<int>(renderer_->height_ * original_aspect);
  }

  // 画像サイズが変更された場合はバッファを再確保
  if (scaled_width_ != input_width_ || scaled_height_ != input_height_) {
    input_width_ = scaled_width_;
    input_height_ = scaled_height_;
    image_.reset(new uint8_t[input_width_ * input_height_ * 4]);
    RTC_LOG(LS_INFO) << "Sixelレンダラー: フレームサイズ変更 "
                     << original_width_ << "x" << original_height_ << " → "
                     << input_width_ << "x" << input_height_;
  }

  // フレームをI420に変換
  webrtc::scoped_refptr<webrtc::I420BufferInterface> buffer_if =
      frame.video_frame_buffer()->ToI420();

  // スケーリング用のI420バッファを作成
  webrtc::scoped_refptr<webrtc::I420Buffer> scaled_buffer =
      webrtc::I420Buffer::Create(scaled_width_, scaled_height_);

  // libyuvを使ってスケーリング
  libyuv::I420Scale(buffer_if->DataY(), buffer_if->StrideY(),
                    buffer_if->DataU(), buffer_if->StrideU(),
                    buffer_if->DataV(), buffer_if->StrideV(),
                    buffer_if->width(), buffer_if->height(),
                    scaled_buffer->MutableDataY(), scaled_buffer->StrideY(),
                    scaled_buffer->MutableDataU(), scaled_buffer->StrideU(),
                    scaled_buffer->MutableDataV(), scaled_buffer->StrideV(),
                    scaled_width_, scaled_height_, libyuv::kFilterBilinear);

  // スケーリングしたフレームをARGB形式に変換
  libyuv::ConvertFromI420(scaled_buffer->DataY(), scaled_buffer->StrideY(),
                          scaled_buffer->DataU(), scaled_buffer->StrideU(),
                          scaled_buffer->DataV(), scaled_buffer->StrideV(),
                          image_.get(), input_width_ * 4, scaled_width_,
                          scaled_height_, libyuv::FOURCC_ARGB);

  has_new_frame_ = true;
}

webrtc::Mutex* SixelRenderer::Sink::GetMutex() {
  return &frame_params_lock_;
}

bool SixelRenderer::Sink::HasNewFrame() {
  return has_new_frame_;
}

void SixelRenderer::Sink::ResetNewFrame() {
  has_new_frame_ = false;
}

int SixelRenderer::Sink::GetFrameWidth() {
  return input_width_;
}

int SixelRenderer::Sink::GetFrameHeight() {
  return input_height_;
}

int SixelRenderer::Sink::GetOriginalWidth() {
  return original_width_;
}

int SixelRenderer::Sink::GetOriginalHeight() {
  return original_height_;
}

uint8_t* SixelRenderer::Sink::GetImage() {
  return image_.get();
}

void SixelRenderer::AddTrack(webrtc::VideoTrackInterface* track) {
  std::unique_ptr<Sink> sink(new Sink(this, track));
  webrtc::MutexLock lock(&sinks_lock_);
  sinks_.push_back(std::make_pair(track, std::move(sink)));
  RTC_LOG(LS_INFO) << "Sixelレンダラー: トラック追加";
}

void SixelRenderer::RemoveTrack(webrtc::VideoTrackInterface* track) {
  webrtc::MutexLock lock(&sinks_lock_);
  sinks_.erase(
      std::remove_if(sinks_.begin(), sinks_.end(),
                     [track](const VideoTrackSinkVector::value_type& sink) {
                       return sink.first == track;
                     }),
      sinks_.end());
  RTC_LOG(LS_INFO) << "Sixelレンダラー: トラック削除";
}