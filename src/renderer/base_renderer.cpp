#include "sora/renderer/base_renderer.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <memory>
#include <thread>
#include <utility>
#include <vector>

// WebRTC
#include <api/media_stream_interface.h>
#include <api/scoped_refptr.h>
#include <api/video/i420_buffer.h>
#include <api/video/video_frame.h>
#include <api/video/video_frame_buffer.h>
#include <api/video/video_rotation.h>
#include <api/video/video_source_interface.h>
#include <rtc_base/logging.h>
#include <rtc_base/synchronization/mutex.h>

// libyuv
#include <libyuv/convert_from.h>
#include <libyuv/planar_functions.h>
#include <libyuv/video_common.h>

namespace sora {

static constexpr float STD_ASPECT = 1.33f;   // 4:3
static constexpr float WIDE_ASPECT = 1.78f;  // 16:9

BaseRenderer::BaseRenderer(int width, int height, int fps)
    : running_(false),
      width_(width),
      height_(height),
      fps_(fps),
      rows_(1),
      cols_(1) {}

BaseRenderer::~BaseRenderer() {
  Stop();
}

void BaseRenderer::Start() {
  Stop();
  running_ = true;
  thread_.reset(new std::thread([this]() { RenderThread(); }));
}

void BaseRenderer::Stop() {
  if (thread_ != nullptr) {
    running_ = false;
    thread_->join();
    thread_ = nullptr;
  }
}

webrtc::Mutex* BaseRenderer::GetMutex() {
  return &sinks_lock_;
}

void BaseRenderer::SetSize(int width, int height) {
  webrtc::MutexLock lock(&sinks_lock_);
  width_ = width;
  height_ = height;
  SetOutlines();
}

void BaseRenderer::RenderThread() {
  RenderThreadStarted();

  std::vector<uint8_t> image;

  while (running_) {
    std::chrono::steady_clock::time_point frame_start;
    std::vector<SinkInfo> sink_infos;
    int canvas_width = 0;
    int canvas_height = 0;
    {
      webrtc::MutexLock lock(&sinks_lock_);
      // SetSize() と競合してもキャンバス寸法と描画バッファのサイズが
      // 食い違わないよう、Sink の合成まで同じロック下で処理する。
      // 入力サイズの変化は Sink::OnFrame() から直接 SetOutlines() を呼ばず、
      // ここで検出してから再計算する。OnFrame() は Sink の frame_params_lock_
      // を保持中に呼ばれるため、SetOutlines() を直接呼ぶと非再帰ミューテックスの
      // 自己デッドロックと、sinks_lock_ 未保持でのレイアウト変更という競合を
      // 起こすためである。
      bool outlines_dirty = false;
      for (const VideoTrackSinkVector::value_type& sinks : sinks_) {
        Sink* sink = sinks.second.get();
        webrtc::MutexLock frame_lock(sink->GetMutex());
        if (sink->ConsumeInputSizeDirty()) {
          outlines_dirty = true;
        }
      }
      if (outlines_dirty) {
        SetOutlines();
      }
      canvas_width = width_;
      canvas_height = height_;
      image.resize(static_cast<size_t>(canvas_width) *
                   static_cast<size_t>(canvas_height) * 4);
      memset(image.data(), 0, image.size());
      frame_start = std::chrono::steady_clock::now();

      for (const VideoTrackSinkVector::value_type& sinks : sinks_) {
        Sink* sink = sinks.second.get();

        webrtc::MutexLock frame_lock(sink->GetMutex());

        if (sink->GetOutlineChanged()) {
          continue;
        }

        int width = sink->GetFrameWidth();
        int height = sink->GetFrameHeight();

        if (width == 0 || height == 0) {
          continue;
        }

        libyuv::ARGBCopy(sink->GetImage(), width * 4,
                         image.data() + sink->GetOffsetX() * 4 +
                             sink->GetOffsetY() * canvas_width * 4,
                         canvas_width * 4, width, height);

        SinkInfo info;
        info.offset_x = sink->GetOffsetX();
        info.offset_y = sink->GetOffsetY();
        info.input_width = sink->GetInputWidth();
        info.input_height = sink->GetInputHeight();
        info.frame_width = sink->GetFrameWidth();
        info.frame_height = sink->GetFrameHeight();
        info.width = sink->GetWidth();
        info.height = sink->GetHeight();
        sink_infos.push_back(info);
      }
    }

    Render(image.data(), canvas_width, canvas_height, sink_infos);

    // フレームレート制御
    auto frame_end = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                       frame_end - frame_start)
                       .count();
    int frame_interval = 1000 / fps_;
    if (elapsed < frame_interval) {
      std::this_thread::sleep_for(
          std::chrono::milliseconds(frame_interval - elapsed));
    }
  }

  RenderThreadFinished();
}

BaseRenderer::Sink::Sink(BaseRenderer* renderer,
                         webrtc::VideoTrackInterface* track)
    : renderer_(renderer),
      track_(track),
      outline_offset_x_(0),
      outline_offset_y_(0),
      outline_width_(0),
      outline_height_(0),
      outline_changed_(false),
      input_size_dirty_(false),
      input_width_(0),
      input_height_(0),
      rotation_(webrtc::kVideoRotation_0),
      scaled_(false),
      width_(0),
      height_(0) {
  track_->AddOrUpdateSink(this, webrtc::VideoSinkWants());
}

BaseRenderer::Sink::~Sink() {
  track_->RemoveSink(this);
}

void BaseRenderer::Sink::OnFrame(const webrtc::VideoFrame& frame) {
  // 枠の未確定チェックを frame_params_lock_ 保護下で行い、
  // SetOutlineRect() の書き込みと同期させる。
  webrtc::MutexLock lock(GetMutex());
  if (outline_width_ == 0 || outline_height_ == 0)
    return;
  if (frame.width() == 0 || frame.height() == 0)
    return;
  // 90° / 270° 回転では表示寸法の幅と高さが入れ替わる。
  bool rotated = frame.rotation() == webrtc::kVideoRotation_90 ||
                 frame.rotation() == webrtc::kVideoRotation_270;
  bool was_rotated = IsRotated90Or270();
  // 入力サイズまたは回転状態の変化は、枠割りの再計算 (SetOutlines) の
  // トリガーとして記録する。寸法・アスペクトが変わらない 180° 回転と
  // 90° ↔ 270° の遷移はトリガーに含めない。
  bool size_or_rotation_changed = frame.width() != input_width_ ||
                                  frame.height() != input_height_ ||
                                  rotated != was_rotated;
  if (size_or_rotation_changed)
    input_size_dirty_ = true;
  if (outline_changed_ || size_or_rotation_changed) {
    int width, height;
    // 90° / 270° 回転では表示アスペクトが入れ替わるため、
    // 回転後寸法からアスペクトを算出する。
    float frame_aspect;
    if (rotated) {
      frame_aspect = (float)frame.height() / (float)frame.width();
    } else {
      frame_aspect = (float)frame.width() / (float)frame.height();
    }
    if (frame_aspect > outline_aspect_) {
      width = outline_width_;
      height = width / frame_aspect;
      offset_x_ = 0;
      offset_y_ = (outline_height_ - height) / 2;
    } else {
      height = outline_height_;
      width = height * frame_aspect;
      offset_x_ = (outline_width_ - width) / 2;
      offset_y_ = 0;
    }
    if (width_ != width || height_ != height) {
      width_ = width;
      height_ = height;
    }
    input_width_ = frame.width();
    input_height_ = frame.height();
    // scaled_ の判定は回転後の表示寸法と枠の寸法を比較する。
    // 90° / 270° 回転では表示寸法の幅と高さが入れ替わるため、
    // 回転後に枠より大きくなる映像を縮小対象に含める。
    int display_width = rotated ? input_height_ : input_width_;
    scaled_ = width_ < display_width;
    if (scaled_) {
      image_.reset(new uint8_t[width_ * height_ * 4]);
    } else {
      image_.reset(new uint8_t[input_width_ * input_height_ * 4]);
    }
    RTC_LOG(LS_VERBOSE) << __func__ << ": scaled_=" << scaled_;
    outline_changed_ = false;
  }
  // 回転は有効フレームごとに記録する。90° ↔ 270° の遷移は表示寸法・
  // アスペクトが変わらないため枠割りの再計算は不要だが、
  // 記録は最新の回転値に保つ。
  rotation_ = frame.rotation();
  webrtc::scoped_refptr<webrtc::I420BufferInterface> buffer_if;
  if (scaled_) {
    // 回転 90° / 270° では回転後に幅と高さが入れ替わるため、
    // 回転前の寸法 (回転後表示寸法の幅と高さを入れ替えた寸法) に
    // 縮小してから回転し、表示寸法に一致させる。
    int scale_width = width_;
    int scale_height = height_;
    if (rotated) {
      scale_width = height_;
      scale_height = width_;
    }
    webrtc::scoped_refptr<webrtc::I420Buffer> buffer =
        webrtc::I420Buffer::Create(scale_width, scale_height);
    buffer->ScaleFrom(*frame.video_frame_buffer()->ToI420());
    if (frame.rotation() != webrtc::kVideoRotation_0) {
      buffer = webrtc::I420Buffer::Rotate(*buffer, frame.rotation());
    }
    buffer_if = buffer;
  } else {
    // 非 scaled 経路でも回転を適用する。
    if (frame.rotation() != webrtc::kVideoRotation_0) {
      buffer_if = webrtc::I420Buffer::Rotate(
          *frame.video_frame_buffer()->ToI420(), frame.rotation());
    } else {
      buffer_if = frame.video_frame_buffer()->ToI420();
    }
  }
  // ストライドと出力寸法は回転後の表示寸法に合わせる。
  libyuv::ConvertFromI420(
      buffer_if->DataY(), buffer_if->StrideY(), buffer_if->DataU(),
      buffer_if->StrideU(), buffer_if->DataV(), buffer_if->StrideV(),
      image_.get(), buffer_if->width() * 4, buffer_if->width(),
      buffer_if->height(), libyuv::FOURCC_ARGB);
}

void BaseRenderer::Sink::SetOutlineRect(int x, int y, int width, int height) {
  // outline_offset_x_ / outline_offset_y_ の書き込みと early return 判定を
  // frame_params_lock_ 保護下に置き、RenderThread() の合成ループと
  // OnFrame() の読みと同期させる。
  webrtc::MutexLock lock(GetMutex());
  outline_offset_x_ = x;
  outline_offset_y_ = y;
  if (outline_width_ == width && outline_height_ == height) {
    return;
  }
  offset_y_ = 0;
  offset_x_ = 0;
  outline_width_ = width;
  outline_height_ = height;
  outline_aspect_ = (float)outline_width_ / (float)outline_height_;
  outline_changed_ = true;
}

webrtc::Mutex* BaseRenderer::Sink::GetMutex() {
  return &frame_params_lock_;
}

bool BaseRenderer::Sink::GetOutlineChanged() {
  return outline_changed_;
}

bool BaseRenderer::Sink::ConsumeInputSizeDirty() {
  // 確認とリセットを 1 回の呼び出しにまとめ、リセット忘れで
  // 毎フレーム SetOutlines() が呼ばれ続けることを防ぐ。
  bool dirty = input_size_dirty_;
  input_size_dirty_ = false;
  return dirty;
}

int BaseRenderer::Sink::GetOffsetX() {
  return outline_offset_x_ + offset_x_;
}

int BaseRenderer::Sink::GetOffsetY() {
  return outline_offset_y_ + offset_y_;
}

int BaseRenderer::Sink::GetInputWidth() {
  return input_width_;
}

int BaseRenderer::Sink::GetInputHeight() {
  return input_height_;
}

int BaseRenderer::Sink::GetFrameWidth() {
  if (scaled_)
    return width_;
  // 90° / 270° 回転では表示寸法の幅と高さが入れ替わる。
  return IsRotated90Or270() ? input_height_ : input_width_;
}

int BaseRenderer::Sink::GetFrameHeight() {
  if (scaled_)
    return height_;
  // 90° / 270° 回転では表示寸法の幅と高さが入れ替わる。
  return IsRotated90Or270() ? input_width_ : input_height_;
}

int BaseRenderer::Sink::GetWidth() {
  return width_;
}

int BaseRenderer::Sink::GetHeight() {
  return height_;
}

uint8_t* BaseRenderer::Sink::GetImage() {
  return image_.get();
}

bool BaseRenderer::Sink::IsRotated90Or270() {
  return rotation_ == webrtc::kVideoRotation_90 ||
         rotation_ == webrtc::kVideoRotation_270;
}

void BaseRenderer::SetOutlines() {
  float window_aspect = (float)width_ / (float)height_;
  bool window_is_wide = window_aspect > ((STD_ASPECT + WIDE_ASPECT) / 2.0);
  float frame_aspect = window_is_wide ? WIDE_ASPECT : STD_ASPECT;
  // 入力サイズが確定している最初の Sink を代表 Sink とし、
  // その実測アスペクトを frame_aspect に採用する。
  // 入力サイズが確定していない間は STD_ASPECT / WIDE_ASPECT の 2 択に
  // フォールバックする。
  for (const VideoTrackSinkVector::value_type& sinks : sinks_) {
    Sink* sink = sinks.second.get();
    webrtc::MutexLock frame_lock(sink->GetMutex());
    if (sink->GetInputWidth() != 0 && sink->GetInputHeight() != 0) {
      int input_width = sink->GetInputWidth();
      int input_height = sink->GetInputHeight();
      // 90° / 270° 回転では表示アスペクトが入れ替わる。
      if (sink->IsRotated90Or270()) {
        std::swap(input_width, input_height);
      }
      frame_aspect = (float)input_width / (float)input_height;
      break;
    }
  }
  int rows = 1;
  int cols = 1;
  if (window_aspect >= frame_aspect) {
    int times = std::floor(window_aspect / frame_aspect);
    if (times < 1)
      times = 1;
    while (rows * cols < sinks_.size()) {
      if (times < (cols / rows)) {
        rows++;
      } else {
        cols++;
      }
    }
  } else {
    int times = std::floor(frame_aspect / window_aspect);
    if (times < 1)
      times = 1;
    while (rows * cols < sinks_.size()) {
      if (times < (rows / cols)) {
        cols++;
      } else {
        rows++;
      }
    }
  }
  RTC_LOG(LS_VERBOSE) << __func__ << " rows:" << rows << " cols:" << cols;
  // cols × rows で等分割した枠を映像アスペクトに合わせて共通縮小し、
  // ウィンドウ中央寄せする。枠内 letterbox の分がウィンドウ外周に集約される。
  float raw_outline_width_f = (float)width_ / cols;
  float raw_outline_height_f = (float)height_ / rows;
  float raw_outline_aspect = raw_outline_width_f / raw_outline_height_f;
  float ideal_outline_width_f;
  float ideal_outline_height_f;
  if (frame_aspect > raw_outline_aspect) {
    ideal_outline_width_f = raw_outline_width_f;
    ideal_outline_height_f = ideal_outline_width_f / frame_aspect;
  } else {
    ideal_outline_height_f = raw_outline_height_f;
    ideal_outline_width_f = ideal_outline_height_f * frame_aspect;
  }
  // 負のオフセットが Sink::SetOutlineRect() に渡ると、合成ループが
  // 描画バッファの手前へ書き出すアンダーランを起こすため、
  // float の丸めで負になりうるオフセットは必ず非負にクランプする。
  float grid_offset_x_f =
      std::max(0.0f, ((float)width_ - ideal_outline_width_f * cols) / 2.0f);
  float grid_offset_y_f =
      std::max(0.0f, ((float)height_ - ideal_outline_height_f * rows) / 2.0f);
  int sinks_count = sinks_.size();
  for (int i = 0; i < sinks_count; i++) {
    Sink* sink = sinks_[i].second.get();
    int col = i % cols;
    int row = i / cols;
    // 枠の位置とサイズを int に丸めて算出し、隣接 cell 間に隙間を作らない。
    // 右端・下端は float の丸めでウィンドウを超えうるため、
    // 描画バッファ境界を超えないようウィンドウ幅・高さでクランプする。
    float x_begin_f = std::min(
        (float)width_, grid_offset_x_f + (float)col * ideal_outline_width_f);
    float y_begin_f = std::min(
        (float)height_, grid_offset_y_f + (float)row * ideal_outline_height_f);
    float x_end_f =
        std::min((float)width_,
                 grid_offset_x_f + (float)(col + 1) * ideal_outline_width_f);
    float y_end_f =
        std::min((float)height_,
                 grid_offset_y_f + (float)(row + 1) * ideal_outline_height_f);
    int offset_x = std::floor(x_begin_f);
    int offset_y = std::floor(y_begin_f);
    int outline_width = std::floor(x_end_f) - offset_x;
    int outline_height = std::floor(y_end_f) - offset_y;
    sink->SetOutlineRect(offset_x, offset_y, outline_width, outline_height);
    RTC_LOG(LS_VERBOSE) << __func__ << " offset_x:" << offset_x
                        << " offset_y:" << offset_y
                        << " outline_width:" << outline_width
                        << " outline_height:" << outline_height;
  }
  rows_ = rows;
  cols_ = cols;
}

void BaseRenderer::AddTrack(webrtc::VideoTrackInterface* track) {
  std::unique_ptr<Sink> sink(new Sink(this, track));
  webrtc::MutexLock lock(&sinks_lock_);
  sinks_.push_back(std::make_pair(track, std::move(sink)));
  SetOutlines();
}

void BaseRenderer::RemoveTrack(webrtc::VideoTrackInterface* track) {
  webrtc::MutexLock lock(&sinks_lock_);
  sinks_.erase(
      std::remove_if(sinks_.begin(), sinks_.end(),
                     [track](const VideoTrackSinkVector::value_type& sink) {
                       return sink.first == track;
                     }),
      sinks_.end());
  SetOutlines();
}

}  // namespace sora
