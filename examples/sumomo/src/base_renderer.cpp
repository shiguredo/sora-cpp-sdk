#include "base_renderer.h"

#include <cmath>
#include <csignal>

// WebRTC
#include <api/video/i420_buffer.h>
#include <libyuv/convert_from.h>
#include <libyuv/planar_functions.h>
#include <libyuv/video_common.h>
#include <rtc_base/logging.h>

#define STD_ASPECT 1.33
#define WIDE_ASPECT 1.78
#define FRAME_INTERVAL (1000 / 30)

BaseRenderer::BaseRenderer(int width, int height)
    : running_(false), width_(width), height_(height), rows_(1), cols_(1) {}

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

  std::unique_ptr<uint8_t> image(new uint8_t[width_ * height_ * 4]);

  while (running_) {
    memset(image.get(), 0, width_ * height_ * 4);

    auto frame_start = std::chrono::steady_clock::now();
    std::vector<SinkInfo> sink_infos;
    {
      webrtc::MutexLock lock(&sinks_lock_);
      for (const VideoTrackSinkVector::value_type& sinks : sinks_) {
        Sink* sink = sinks.second.get();

        webrtc::MutexLock frame_lock(sink->GetMutex());

        if (!sink->GetOutlineChanged())
          continue;

        int width = sink->GetFrameWidth();
        int height = sink->GetFrameHeight();

        if (width == 0 || height == 0)
          continue;

        libyuv::ARGBCopy(sink->GetImage(), width * 4,
                         image.get() + sink->GetOffsetX() * 4 +
                             sink->GetOffsetY() * width_ * 4,
                         width_ * 4, width, height);

        SinkInfo info;
        info.offset_x = sink->GetOffsetX();
        info.offset_y = sink->GetOffsetY();
        info.input_width = sink->GetInputWidth();
        info.input_height = sink->GetInputHeight();
        info.frame_width = sink->GetFrameWidth();
        info.frame_height = sink->GetFrameHeight();
        info.width = width;
        info.height = height;
        sink_infos.push_back(info);
      }
    }

    Render(image.get(), width_, height_, sink_infos);

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
      input_width_(0),
      input_height_(0),
      scaled_(false),
      width_(0),
      height_(0) {
  track_->AddOrUpdateSink(this, webrtc::VideoSinkWants());
}

BaseRenderer::Sink::~Sink() {
  track_->RemoveSink(this);
}

void BaseRenderer::Sink::OnFrame(const webrtc::VideoFrame& frame) {
  if (outline_width_ == 0 || outline_height_ == 0)
    return;
  if (frame.width() == 0 || frame.height() == 0)
    return;
  webrtc::MutexLock lock(GetMutex());
  if (outline_changed_ || frame.width() != input_width_ ||
      frame.height() != input_height_) {
    int width, height;
    float frame_aspect = (float)frame.width() / (float)frame.height();
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
    scaled_ = width_ < input_width_;
    if (scaled_) {
      image_.reset(new uint8_t[width_ * height_ * 4]);
    } else {
      image_.reset(new uint8_t[input_width_ * input_height_ * 4]);
    }
    RTC_LOG(LS_VERBOSE) << __FUNCTION__ << ": scaled_=" << scaled_;
    outline_changed_ = false;
  }
  webrtc::scoped_refptr<webrtc::I420BufferInterface> buffer_if;
  if (scaled_) {
    webrtc::scoped_refptr<webrtc::I420Buffer> buffer =
        webrtc::I420Buffer::Create(width_, height_);
    buffer->ScaleFrom(*frame.video_frame_buffer()->ToI420());
    if (frame.rotation() != webrtc::kVideoRotation_0) {
      buffer = webrtc::I420Buffer::Rotate(*buffer, frame.rotation());
    }
    buffer_if = buffer;
  } else {
    buffer_if = frame.video_frame_buffer()->ToI420();
  }
  libyuv::ConvertFromI420(
      buffer_if->DataY(), buffer_if->StrideY(), buffer_if->DataU(),
      buffer_if->StrideU(), buffer_if->DataV(), buffer_if->StrideV(),
      image_.get(), (scaled_ ? width_ : input_width_) * 4, buffer_if->width(),
      buffer_if->height(), libyuv::FOURCC_ARGB);
}

void BaseRenderer::Sink::SetOutlineRect(int x, int y, int width, int height) {
  outline_offset_x_ = x;
  outline_offset_y_ = y;
  if (outline_width_ == width && outline_height_ == height) {
    return;
  }
  webrtc::MutexLock lock(GetMutex());
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
  return !outline_changed_;
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
  return scaled_ ? width_ : input_width_;
}

int BaseRenderer::Sink::GetFrameHeight() {
  return scaled_ ? height_ : input_height_;
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

void BaseRenderer::SetOutlines() {
  float window_aspect = (float)width_ / (float)height_;
  bool window_is_wide = window_aspect > ((STD_ASPECT + WIDE_ASPECT) / 2.0);
  float frame_aspect = window_is_wide ? WIDE_ASPECT : STD_ASPECT;
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
  RTC_LOG(LS_VERBOSE) << __FUNCTION__ << " rows:" << rows << " cols:" << cols;
  int outline_width = std::floor(width_ / cols);
  int outline_height = std::floor(height_ / rows);
  int sinks_count = sinks_.size();
  for (int i = 0; i < sinks_count; i++) {
    Sink* sink = sinks_[i].second.get();
    int offset_x = outline_width * (i % cols);
    int offset_y = outline_height * std::floor(i / cols);
    sink->SetOutlineRect(offset_x, offset_y, outline_width, outline_height);
    RTC_LOG(LS_VERBOSE) << __FUNCTION__ << " offset_x:" << offset_x
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
