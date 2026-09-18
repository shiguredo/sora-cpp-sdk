#ifndef SCREEN_VIDEO_CAPTURER_H_
#define SCREEN_VIDEO_CAPTURER_H_

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

// WebRTC
#include <api/scoped_refptr.h>
#include <api/video/video_frame.h>
#include <modules/desktop_capture/desktop_capturer.h>
#include <modules/desktop_capture/desktop_frame.h>
#include <rtc_base/platform_thread.h>

// Sora C++ SDK
#include <sora/scalable_track_source.h>

// 画面をキャプチャして Sora へ配信するためのビデオソース
//
// webrtc::DesktopCapturer で取得した画面フレームにマウスカーソルを合成し、
// --resolution で指定されたサイズに収まるようアスペクト比を維持したまま縮小してから
// ScalableVideoTrackSource::OnFrame へ渡す。
// キャプチャは専用スレッドで行い、CPU 使用率が高くなりすぎないように
// 前回のキャプチャに要した時間から次のキャプチャまでの待ち時間を計算する。
class ScreenVideoCapturer : public sora::ScalableVideoTrackSource,
                            public webrtc::DesktopCapturer::Callback {
 public:
  // キャプチャの準備を行い、準備できなかった場合は std::runtime_error を投げる
  // 参照カウントのために webrtc::make_ref_counted から呼び出せるように public にしている
  // 通常は Create() を利用すること
  ScreenVideoCapturer(webrtc::DesktopCapturer::SourceId source_id,
                      size_t max_width,
                      size_t max_height,
                      size_t target_fps);
  // 指定されたソースのキャプチャを準備する
  // 準備できなかった場合は nullptr を返す
  // キャプチャスレッドはこの関数を抜けた後に StartCapture() を呼んで開始する
  static webrtc::scoped_refptr<ScreenVideoCapturer> Create(
      webrtc::DesktopCapturer::SourceId source_id,
      size_t max_width,
      size_t max_height,
      size_t target_fps);
  // キャプチャ可能なソース (画面) の一覧を取得する
  static bool GetSourceList(webrtc::DesktopCapturer::SourceList* sources);
  // キャプチャ可能なソース (画面) の一覧をログ出力用の文字列で取得する
  static std::string GetSourceListString();

  // キャプチャスレッドを開始する
  //
  // コンストラクタの中でキャプチャスレッドを開始すると、参照カウントを行う
  // webrtc::RefCountedObject が vtable を設定する前にキャプチャしたフレームを
  // 処理してしまうため、オブジェクトの生成が完了してから呼び出すこと。
  void StartCapture();

  ~ScreenVideoCapturer() override;

 private:
  static webrtc::DesktopCaptureOptions CreateDesktopCaptureOptions();
  void CaptureThread();
  void CaptureProcess();
  void OnCaptureResult(webrtc::DesktopCapturer::Result result,
                       std::unique_ptr<webrtc::DesktopFrame> frame) override;

  const size_t max_width_;
  const size_t max_height_;
  // 出力するフレームの横幅と縦幅
  // キャプチャしたフレームのサイズが変わったときに計算し直す
  size_t capture_width_;
  size_t capture_height_;
  // 次のキャプチャまでの待ち時間を計算するために利用する 1 フレームの目標時間 (ミリ秒)
  const int requested_frame_duration_;
  // キャプチャで利用してよい CPU 使用率の上限 (パーセント)
  const int max_cpu_consumption_percentage_;
  // 直前にキャプチャしたフレームのサイズ
  // サイズが変わったときに出力用のバッファを作り直すために利用する
  webrtc::DesktopSize previous_frame_size_;
  // 拡大縮小したフレームを格納するバッファ
  // フレームサイズが変わったときのみ作り直す
  std::unique_ptr<webrtc::DesktopFrame> output_frame_;
  // キャプチャスレッドを終了させるためのフラグ
  std::atomic<bool> quit_{false};
  webrtc::PlatformThread capture_thread_;
  std::unique_ptr<webrtc::DesktopCapturer> capturer_;
};

#endif  // SCREEN_VIDEO_CAPTURER_H_
