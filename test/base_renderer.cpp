// BaseRenderer::Sink::OnFrame のスケール経路を検証するテスト
// 枠の寸法に合わせて映像を常に拡大縮小して描画することを検証する
// 映像の注入には実フレームを生成するテスト用ソースと実トラックを使うため、
// モックやスタブは利用していない
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstring>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>
#include <vector>

// WebRTC
#include <api/make_ref_counted.h>
#include <api/media_stream_interface.h>
#include <api/scoped_refptr.h>
#include <api/video/i420_buffer.h>
#include <api/video/video_frame.h>
#include <api/video/video_rotation.h>
#include <media/base/adapted_video_track_source.h>
#include <pc/video_track.h>
#include <rtc_base/thread.h>
#include <rtc_base/time_utils.h>
#include <catch2/catch_test_macros.hpp>

#include "sora/renderer/base_renderer.h"

namespace {

// 任意の寸法・回転の映像フレームを注入できるテスト用映像ソース
// AdaptedVideoTrackSource::OnFrame() を公開して実フレームを注入する
class TestVideoSource : public webrtc::AdaptedVideoTrackSource {
 public:
  bool is_screencast() const override { return false; }
  std::optional<bool> needs_denoising() const override { return false; }
  webrtc::MediaSourceInterface::SourceState state() const override {
    return webrtc::MediaSourceInterface::kLive;
  }
  bool remote() const override { return false; }

  // 単色 (Y=255, U=0, V=0) のフレームを注入する
  // 黒 (0, 0, 0) でない単色にすることで、letterbox の黒帯と映像領域を
  // ピクセル単位で区別できる
  void InjectFrame(int width, int height, webrtc::VideoRotation rotation) {
    webrtc::scoped_refptr<webrtc::I420Buffer> buffer =
        webrtc::I420Buffer::Create(width, height);
    std::memset(buffer->MutableDataY(), 255,
                buffer->StrideY() * buffer->height());
    std::memset(buffer->MutableDataU(), 0,
                buffer->StrideU() * ((buffer->height() + 1) / 2));
    std::memset(buffer->MutableDataV(), 0,
                buffer->StrideV() * ((buffer->height() + 1) / 2));
    webrtc::VideoFrame frame = webrtc::VideoFrame::Builder()
                                   .set_video_frame_buffer(buffer)
                                   .set_timestamp_us(webrtc::TimeMicros())
                                   .set_rotation(rotation)
                                   .build();
    OnFrame(frame);
  }
};

// Render() で描画結果 (SinkInfo とキャンバスのピクセル) を収集するテスト用レンダラー
class TestRenderer : public sora::BaseRenderer {
 public:
  TestRenderer(int width, int height) : sora::BaseRenderer(width, height, 30) {
    Start();
  }
  ~TestRenderer() override { Stop(); }

  void RenderThreadStarted() override {}
  void RenderThreadFinished() override {}

  void Render(
      uint8_t* image,
      int width,
      int height,
      const std::vector<sora::BaseRenderer::SinkInfo>& sink_infos) override {
    std::lock_guard<std::mutex> lock(mutex_);
    canvas_width_ = width;
    canvas_height_ = height;
    canvas_.assign(image, image + static_cast<size_t>(width) * height * 4);
    sink_infos_ = sink_infos;
    cv_.notify_all();
  }

  // 最初の Sink のオフセットとフレーム寸法が期待値と一致するまで待つ
  // 描画ループは fps 周期で回るため、フレーム注入後 1 周期以内に反映される
  bool WaitForSinkRect(int offset_x,
                       int offset_y,
                       int width,
                       int height,
                       int timeout_ms = 5000) {
    std::unique_lock<std::mutex> lock(mutex_);
    return cv_.wait_for(lock, std::chrono::milliseconds(timeout_ms), [&] {
      if (sink_infos_.empty()) {
        return false;
      }
      const sora::BaseRenderer::SinkInfo& info = sink_infos_[0];
      return info.offset_x == offset_x && info.offset_y == offset_y &&
             info.frame_width == width && info.frame_height == height;
    });
  }

  // 指定した矩形領域に映像 (非黒) のピクセルが含まれることを確認する
  bool RegionHasVideo(int x, int y, int width, int height) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return RegionHasVideoLocked(x, y, width, height);
  }

  // 指定した矩形領域がすべて黒 (0, 0, 0) であることを確認する
  bool RegionIsBlack(int x, int y, int width, int height) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return !RegionHasVideoLocked(x, y, width, height);
  }

 private:
  bool RegionHasVideoLocked(int x, int y, int width, int height) const {
    if (x < 0 || y < 0 || x + width > canvas_width_ ||
        y + height > canvas_height_) {
      return false;
    }
    for (int j = y; j < y + height; j++) {
      for (int i = x; i < x + width; i++) {
        size_t idx = (static_cast<size_t>(j) * canvas_width_ + i) * 4;
        if (canvas_[idx] != 0 || canvas_[idx + 1] != 0 ||
            canvas_[idx + 2] != 0) {
          return true;
        }
      }
    }
    return false;
  }

  mutable std::mutex mutex_;
  std::condition_variable cv_;
  int canvas_width_ = 0;
  int canvas_height_ = 0;
  std::vector<uint8_t> canvas_;
  std::vector<sora::BaseRenderer::SinkInfo> sink_infos_;
};

// トラックを作成してレンダラーに追加する
// VideoTrack はワーカースレッド上でのみ操作できるため、BlockingCall で
// ワーカースレッド上に処理を寄せる
webrtc::scoped_refptr<TestVideoSource> CreateTrackAndAddSink(
    TestRenderer& renderer,
    webrtc::Thread* worker,
    webrtc::scoped_refptr<webrtc::VideoTrack>* track) {
  webrtc::scoped_refptr<TestVideoSource> source =
      webrtc::make_ref_counted<TestVideoSource>();
  worker->BlockingCall([&] {
    *track = webrtc::VideoTrack::Create("test", source, worker);
    renderer.AddTrack(track->get());
  });
  return source;
}

// ワーカースレッドを生成してトラックを追加し、テスト終了時に片付ける
struct TrackFixture {
  TestRenderer& renderer;
  std::unique_ptr<webrtc::Thread> worker = webrtc::Thread::Create();
  webrtc::scoped_refptr<webrtc::VideoTrack> track;
  webrtc::scoped_refptr<TestVideoSource> source;

  TrackFixture(TestRenderer& renderer) : renderer(renderer) {
    worker->Start();
    source = CreateTrackAndAddSink(renderer, worker.get(), &track);
  }

  ~TrackFixture() {
    StopInjection();
    worker->BlockingCall([&] { renderer.RemoveTrack(track.get()); });
    track = nullptr;
  }

  // 指定した寸法・回転のフレームを 30fps で注入し続ける
  // 枠割りの再計算 (SetOutlines) はフレーム受信後に非同期で走るため、
  // 1 回だけの注入では Sink が「枠変更中」のままスキップされ続ける。
  // 実カメラのように継続注入することで、再計算後の描画が必ず観測できる
  void StartInjection(int width, int height, webrtc::VideoRotation rotation) {
    StopInjection();
    injecting_.store(true);
    injection_thread_ = std::thread([this, width, height, rotation]() {
      while (injecting_.load()) {
        source->InjectFrame(width, height, rotation);
        std::this_thread::sleep_for(std::chrono::milliseconds(33));
      }
    });
  }

  void StopInjection() {
    injecting_.store(false);
    if (injection_thread_.joinable()) {
      injection_thread_.join();
    }
  }

 private:
  std::atomic<bool> injecting_{true};
  std::thread injection_thread_;
};

}  // namespace

TEST_CASE("BaseRenderer が枠より大きい映像を枠の寸法まで拡大する",
          "[base_renderer]") {
  TestRenderer renderer(2560, 1440);
  TrackFixture fixture(renderer);

  // FHD 映像を 16:9 の枠 2560x1440 に拡大して描画すること
  fixture.StartInjection(1920, 1080, webrtc::kVideoRotation_0);
  REQUIRE(renderer.WaitForSinkRect(0, 0, 2560, 1440));
  // 枠領域全体が映像 (非黒) であり、黒帯が残らないこと
  REQUIRE(renderer.RegionHasVideo(0, 0, 2560, 1440));
}

TEST_CASE("BaseRenderer が枠より小さい映像を縮小して枠に合わせる",
          "[base_renderer]") {
  TestRenderer renderer(640, 360);
  TrackFixture fixture(renderer);

  // FHD 映像を 16:9 の枠 640x360 に縮小して描画すること
  fixture.StartInjection(1920, 1080, webrtc::kVideoRotation_0);
  REQUIRE(renderer.WaitForSinkRect(0, 0, 640, 360));
  REQUIRE(renderer.RegionHasVideo(0, 0, 640, 360));
}

TEST_CASE("BaseRenderer がアスペクトの異なる映像を枠内に letterbox で配置する",
          "[base_renderer]") {
  TestRenderer renderer(2560, 1440);
  TrackFixture fixture(renderer);

  // 4:3 映像を 16:9 の枠に配置すると、左右に letterbox の黒帯が残る
  fixture.StartInjection(640, 480, webrtc::kVideoRotation_0);
  REQUIRE(renderer.WaitForSinkRect(320, 0, 1920, 1440));
  REQUIRE(renderer.RegionHasVideo(320, 0, 1920, 1440));
  REQUIRE(renderer.RegionIsBlack(0, 0, 320, 1440));
  REQUIRE(renderer.RegionIsBlack(2240, 0, 320, 1440));
}

TEST_CASE("BaseRenderer が回転 90° の映像を回転後寸法で枠に合わせる",
          "[base_renderer]") {
  TestRenderer renderer(2560, 1440);
  TrackFixture fixture(renderer);

  // 回転後アスペクト 9:16 のフィット寸法 810x1440 になり、左右に黒帯が残る
  fixture.StartInjection(1920, 1080, webrtc::kVideoRotation_90);
  REQUIRE(renderer.WaitForSinkRect(875, 0, 810, 1440));
  REQUIRE(renderer.RegionHasVideo(875, 0, 810, 1440));
  REQUIRE(renderer.RegionIsBlack(0, 0, 875, 1440));
}

TEST_CASE("BaseRenderer が極小の枠でフィット寸法が 0 になっても abort しない",
          "[base_renderer]") {
  TestRenderer renderer(1, 2);
  TrackFixture fixture(renderer);

  // 枠 1x1 に 16:9 の映像を注入すると、フィット寸法の高さが 0 になる
  // この状態でフレームを処理しても abort しないこと
  // (修正前は 0 寸法の I420Buffer を生成する際に RTC_CHECK で abort した)
  fixture.StartInjection(1920, 1080, webrtc::kVideoRotation_90);
  std::this_thread::sleep_for(std::chrono::milliseconds(200));

  // ウィンドウを戻すと正常に描画できること
  renderer.SetSize(2560, 1440);
  fixture.StartInjection(1920, 1080, webrtc::kVideoRotation_90);
  REQUIRE(renderer.WaitForSinkRect(875, 0, 810, 1440));
  REQUIRE(renderer.RegionHasVideo(875, 0, 810, 1440));
}
