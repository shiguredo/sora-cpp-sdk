#include "screen_video_capturer.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>

// WebRTC
#include <api/make_ref_counted.h>
#include <api/scoped_refptr.h>
#include <api/video/i420_buffer.h>
#include <api/video/video_frame.h>
#include <api/video/video_rotation.h>
#include <libyuv/convert.h>
#include <libyuv/scale.h>
#include <libyuv/scale_argb.h>
#include <modules/desktop_capture/cropped_desktop_frame.h>
#include <modules/desktop_capture/desktop_and_cursor_composer.h>
#include <modules/desktop_capture/desktop_capture_options.h>
#include <modules/desktop_capture/desktop_capturer.h>
#include <modules/desktop_capture/desktop_frame.h>
#include <modules/desktop_capture/desktop_geometry.h>
#include <rtc_base/logging.h>
#include <rtc_base/platform_thread.h>
#include <rtc_base/thread.h>
#include <rtc_base/time_utils.h>
#include <sora/scalable_track_source.h>

std::string ScreenVideoCapturer::GetSourceListString() {
  std::string result;
  webrtc::DesktopCapturer::SourceList sources;
  if (GetSourceList(&sources)) {
    int i = 0;
    for (webrtc::DesktopCapturer::Source& source : sources) {
      // RTC_LOG に整数を直接渡すと libc++ のストリーム用シンボルが必要になるため、
      // 文字列を組み立ててから出力する
      result += std::to_string(i++) + " : " + source.title + "\n";
    }
  }
  return result;
}

bool ScreenVideoCapturer::GetSourceList(
    webrtc::DesktopCapturer::SourceList* sources) {
  std::unique_ptr<webrtc::DesktopCapturer> screen_capturer(
      webrtc::DesktopCapturer::CreateScreenCapturer(
          CreateDesktopCaptureOptions()));
  if (screen_capturer == nullptr) {
    return false;
  }
  return screen_capturer->GetSourceList(sources);
}

webrtc::scoped_refptr<ScreenVideoCapturer> ScreenVideoCapturer::Create(
    webrtc::DesktopCapturer::SourceId source_id,
    size_t max_width,
    size_t max_height,
    size_t target_fps) {
  // キャプチャを準備できなかった場合は webrtc::scoped_refptr を返せないため、
  // コンストラクタで例外を投げてここで受け取る
  try {
    return webrtc::make_ref_counted<ScreenVideoCapturer>(
        source_id, max_width, max_height, target_fps);
  } catch (const std::exception& e) {
    // RTC_LOG に整数を直接渡すと libc++ のストリーム用シンボルが必要になるため、
    // 文字列を組み立ててから出力する
    RTC_LOG(LS_ERROR) << std::string("Failed to create a screen capturer: ") +
                             e.what();
    return nullptr;
  }
}

ScreenVideoCapturer::ScreenVideoCapturer(
    webrtc::DesktopCapturer::SourceId source_id,
    size_t max_width,
    size_t max_height,
    size_t target_fps)
    : sora::ScalableVideoTrackSource(sora::ScalableVideoTrackSourceConfig()),
      max_width_(max_width),
      max_height_(max_height),
      // I420 は偶数サイズしか扱えないため、偶数に切り下げる
      capture_width_(max_width & ~1),
      capture_height_(max_height & ~1),
      requested_frame_duration_(static_cast<int>(1000.0f / target_fps)),
      max_cpu_consumption_percentage_(50) {
  auto options = CreateDesktopCaptureOptions();
  std::unique_ptr<webrtc::DesktopCapturer> screen_capturer(
      webrtc::DesktopCapturer::CreateScreenCapturer(options));
  if (screen_capturer == nullptr) {
    throw std::runtime_error("Failed to create a screen capturer");
  }
  if (!screen_capturer->SelectSource(source_id)) {
    throw std::runtime_error("Failed to select the screen source: source_id=" +
                             std::to_string(source_id));
  }
  // マウスカーソルを合成した状態でフレームを取得する
  std::unique_ptr<webrtc::DesktopAndCursorComposer> capturer(
      new webrtc::DesktopAndCursorComposer(std::move(screen_capturer),
                                           options));
  capturer->Start(this);
  capturer_ = std::move(capturer);
}

void ScreenVideoCapturer::StartCapture() {
  if (!capture_thread_.empty()) {
    return;
  }
  capture_thread_ = webrtc::PlatformThread::SpawnJoinable(
      [this] { CaptureThread(); }, "ScreenCaptureThread",
      webrtc::ThreadAttributes().SetPriority(webrtc::ThreadPriority::kHigh));
}

ScreenVideoCapturer::~ScreenVideoCapturer() {
  if (!capture_thread_.empty()) {
    // デストラクタの本体を抜けるとメンバが破棄されてしまうため、
    // キャプチャスレッドを先に終了させる。
    // CaptureFrame() は 1 フレーム分の待ち時間で必ず戻るので、
    // スレッドの終了を待ち続けることはない。
    quit_ = true;
    capture_thread_.Finalize();
  }
  output_frame_.reset();
  previous_frame_size_.set(0, 0);
  capturer_.reset();
}

webrtc::DesktopCaptureOptions
ScreenVideoCapturer::CreateDesktopCaptureOptions() {
  webrtc::DesktopCaptureOptions options =
      webrtc::DesktopCaptureOptions::CreateDefault();

#if defined(_WIN32)
  // Windows では DirectX を利用したキャプチャを有効にする
  options.set_allow_directx_capturer(true);
#elif defined(__APPLE__)
  // macOS では IOSurface を利用したキャプチャを有効にする
  options.set_allow_iosurface(true);
#endif

  return options;
}

void ScreenVideoCapturer::CaptureThread() {
  while (!quit_) {
    CaptureProcess();
  }
}

void ScreenVideoCapturer::CaptureProcess() {
  if (quit_) {
    return;
  }

  int64_t started_time = webrtc::TimeMillis();
  capturer_->CaptureFrame();
  int last_capture_duration =
      static_cast<int>(webrtc::TimeMillis() - started_time);
  // キャプチャに要した時間から次のキャプチャまでの待ち時間を計算する。
  // CPU 使用率が max_cpu_consumption_percentage_ を超えない範囲で
  // できるだけ目標のフレームレートに近づける。
  int capture_period =
      std::max((last_capture_duration * 100) / max_cpu_consumption_percentage_,
               requested_frame_duration_);
  int delta_time = capture_period - last_capture_duration;
  if (delta_time > 0) {
    webrtc::Thread::SleepMs(delta_time);
  }
}

void ScreenVideoCapturer::OnCaptureResult(
    webrtc::DesktopCapturer::Result result,
    std::unique_ptr<webrtc::DesktopFrame> frame) {
  if (result != webrtc::DesktopCapturer::Result::SUCCESS || frame == nullptr) {
    return;
  }

  // キャプチャしたフレームのサイズが変わったときだけ出力サイズを計算し直す
  if (!previous_frame_size_.equals(frame->size())) {
    capture_width_ = frame->size().width();
    capture_height_ = frame->size().height();
    if (capture_width_ > max_width_) {
      capture_width_ = max_width_;
      capture_height_ =
          frame->size().height() * max_width_ / frame->size().width();
    }
    if (capture_height_ > max_height_) {
      capture_width_ =
          frame->size().width() * max_height_ / frame->size().height();
      capture_height_ = max_height_;
    }
    previous_frame_size_ = frame->size();
    output_frame_.reset();
  }

  // I420 は偶数サイズしか扱えないため、偶数に切り下げる
  webrtc::DesktopSize output_size(capture_width_ & ~1, capture_height_ & ~1);
  if (output_size.is_empty()) {
    output_size.set(2, 2);
  }

  webrtc::scoped_refptr<webrtc::I420Buffer> dst_buffer(
      webrtc::I420Buffer::Create(output_size.width(), output_size.height()));
  dst_buffer->InitializeData();

  if (frame->size().width() <= 2 || frame->size().height() <= 1) {
    // フレームが小さすぎて変換できない場合は黒画像を送信する
  } else {
    const int32_t frame_width = frame->size().width();
    const int32_t frame_height = frame->size().height();

    // ARGBToI420 は偶数サイズしか扱えないため、奇数サイズのフレームは切り落とす
    if (frame_width & 1 || frame_height & 1) {
      frame = webrtc::CreateCroppedDesktopFrame(
          std::move(frame),
          webrtc::DesktopRect::MakeWH(frame_width & ~1, frame_height & ~1));
    }

    const uint8_t* output_data = nullptr;
    int output_stride = 0;
    if (!frame->size().equals(output_size)) {
      // アスペクト比を維持したまま縮小し、余白は黒で埋める
      if (!output_frame_) {
        output_frame_.reset(new webrtc::BasicDesktopFrame(output_size));
      }
      webrtc::DesktopRect output_rect;
      if (static_cast<float>(output_size.width()) /
              static_cast<float>(output_size.height()) <
          static_cast<float>(frame->size().width()) /
              static_cast<float>(frame->size().height())) {
        int32_t output_height = frame->size().height() * output_size.width() /
                                frame->size().width();
        if (output_height > output_size.height()) {
          output_height = output_size.height();
        }
        const int32_t margin_y = (output_size.height() - output_height) / 2;
        output_rect = webrtc::DesktopRect::MakeLTRB(
            0, margin_y, output_size.width(), output_height + margin_y);
      } else {
        int32_t output_width = frame->size().width() * output_size.height() /
                               frame->size().height();
        if (output_width > output_size.width()) {
          output_width = output_size.width();
        }
        const int32_t margin_x = (output_size.width() - output_width) / 2;
        output_rect = webrtc::DesktopRect::MakeLTRB(
            margin_x, 0, output_width + margin_x, output_size.height());
      }
      uint8_t* output_rect_data =
          output_frame_->GetFrameDataAtPos(output_rect.top_left());
      libyuv::ARGBScale(frame->data(), frame->stride(), frame->size().width(),
                        frame->size().height(), output_rect_data,
                        output_frame_->stride(), output_rect.width(),
                        output_rect.height(), libyuv::kFilterBox);
      output_data = output_frame_->data();
      output_stride = output_frame_->stride();
    } else {
      output_data = frame->data();
      output_stride = frame->stride();
    }

    if (libyuv::ARGBToI420(output_data, output_stride,
                           dst_buffer->MutableDataY(), dst_buffer->StrideY(),
                           dst_buffer->MutableDataU(), dst_buffer->StrideU(),
                           dst_buffer->MutableDataV(), dst_buffer->StrideV(),
                           output_size.width(), output_size.height()) < 0) {
      RTC_LOG(LS_ERROR) << "Failed to convert the captured frame to I420";
      return;
    }
  }

  webrtc::VideoFrame capture_frame = webrtc::VideoFrame::Builder()
                                         .set_video_frame_buffer(dst_buffer)
                                         .set_timestamp_rtp(0)
                                         .set_timestamp_ms(webrtc::TimeMillis())
                                         .set_rotation(webrtc::kVideoRotation_0)
                                         .build();
  sora::ScalableVideoTrackSource::OnFrame(capture_frame);
}
