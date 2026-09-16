// BaseRenderer の描画と枠割りを検証するテスト
// BaseRenderer::Sink::OnFrame のスケール経路 (枠の寸法に合わせて映像を常に
// 拡大縮小して描画する処理) と、BaseRenderer::SetOutlines のグリッド計算
// (cols/rows の決定、共通縮小、ウィンドウ中央寄せ、セル座標の累積式) を検証する
// 映像の注入には実フレームを生成するテスト用ソースと実トラックを使うため、
// モックやスタブは利用していない
#include <atomic>
#include <catch2/catch_message.hpp>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

// WebRTC
#include <api/make_ref_counted.h>
#include <api/media_stream_interface.h>
#include <api/scoped_refptr.h>
#include <api/video/adapted_video_track_source.h>
#include <api/video/i420_buffer.h>
#include <api/video/video_frame.h>
#include <api/video/video_rotation.h>
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

// Render() に渡される SinkInfo のうち、枠割りの検証に使う値
// offset_x / offset_y は SetOutlines() が決めた枠の左上座標、
// width / height は枠の寸法 (SinkInfo の frame_width / frame_height と同値)
struct ExpectedRect {
  int offset_x;
  int offset_y;
  int width;
  int height;
};

// 期待値と実測値を失敗メッセージで区別できるよう別の構造体にする
struct ActualRect {
  int offset_x;
  int offset_y;
  int width;
  int height;
};

// 実測した矩形を失敗メッセージ用の文字列にする
std::string RectToActualString(const std::vector<ActualRect>& actual) {
  std::string result = "[";
  for (size_t i = 0; i < actual.size(); i++) {
    if (i != 0) {
      result += ", ";
    }
    result += "(" + std::to_string(actual[i].offset_x) + ", " +
              std::to_string(actual[i].offset_y) + ", " +
              std::to_string(actual[i].width) + ", " +
              std::to_string(actual[i].height) + ")";
  }
  result += "]";
  return result;
}

// 観測した全矩形がウィンドウの描画バッファ内に収まっていることを確認する
// offset が負になったりバッファの外へはみ出したりすると、
// RenderThread() の合成 (ARGBCopy) が描画バッファ外へ書き込む
bool AllRectsInsideWindow(const std::vector<ActualRect>& actual,
                          int window_width,
                          int window_height) {
  for (const ActualRect& rect : actual) {
    if (rect.offset_x < 0 || rect.offset_y < 0 || rect.width < 0 ||
        rect.height < 0) {
      return false;
    }
    // offset + width - 1 で比較し、int のオーバーフローを避ける
    if (rect.width > 0 && rect.offset_x + rect.width - 1 >= window_width) {
      return false;
    }
    if (rect.height > 0 && rect.offset_y + rect.height - 1 >= window_height) {
      return false;
    }
  }
  return true;
}

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

  // 描画結果が条件を満たすまで待つ
  // 描画ループは fps 周期で回るため、フレーム注入後 1 周期以内に反映される
  // 0 寸法の枠は RenderThread() の合成ループでスキップされ SinkInfo に含まれない
  template <typename Predicate>
  bool WaitUntil(Predicate predicate, int timeout_ms = 3000) {
    std::unique_lock<std::mutex> lock(mutex_);
    return cv_.wait_for(lock, std::chrono::milliseconds(timeout_ms),
                        [&]() { return predicate(GetActualRectsLocked()); });
  }

  // 全 Sink のオフセットと枠寸法が期待値と一致するまで待つ
  // 期待数に達しない場合 (0 寸法の枠がスキップされた場合) もタイムアウトで false を返す
  bool WaitForSinkRects(const std::vector<ExpectedRect>& expected,
                        int timeout_ms = 3000) {
    return WaitUntil(
        [&](const std::vector<ActualRect>& actual) {
          if (actual.size() != expected.size()) {
            return false;
          }
          for (size_t i = 0; i < expected.size(); i++) {
            if (actual[i].offset_x != expected[i].offset_x ||
                actual[i].offset_y != expected[i].offset_y ||
                actual[i].width != expected[i].width ||
                actual[i].height != expected[i].height) {
              return false;
            }
          }
          return true;
        },
        timeout_ms);
  }

  // 最初の Sink のオフセットとフレーム寸法が期待値と一致するまで待つ
  bool WaitForSinkRect(int offset_x,
                       int offset_y,
                       int width,
                       int height,
                       int timeout_ms = 3000) {
    return WaitForSinkRects({{offset_x, offset_y, width, height}}, timeout_ms);
  }

  // 待ち合わせに失敗したときの原因 (要素数の不足か値の不一致か) を
  // 切り分けられるよう、最後に観測した矩形を ActualRect で返す
  std::vector<ActualRect> GetActualRects() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return GetActualRectsLocked();
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

  // Render() が収集したキャンバス寸法の検証用アクセサ
  int CanvasWidth() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return canvas_width_;
  }

  int CanvasHeight() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return canvas_height_;
  }

 private:
  // mutex_ を保持した状態で、観測した矩形を ActualRect に変換する
  std::vector<ActualRect> GetActualRectsLocked() const {
    std::vector<ActualRect> actual;
    actual.reserve(sink_infos_.size());
    for (const sora::BaseRenderer::SinkInfo& info : sink_infos_) {
      actual.push_back(
          {info.offset_x, info.offset_y, info.frame_width, info.frame_height});
    }
    return actual;
  }

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
// track_count 本のトラックは同一のワーカースレッド上で作り、同じ寸法の
// フレームを注入して複数 Sink の枠割りを検証できるようにする
struct TrackFixture {
  TestRenderer& renderer;
  std::unique_ptr<webrtc::Thread> worker = webrtc::Thread::Create();
  std::vector<webrtc::scoped_refptr<webrtc::VideoTrack>> tracks;
  std::vector<webrtc::scoped_refptr<TestVideoSource>> sources;

  TrackFixture(TestRenderer& renderer, size_t track_count = 1)
      : renderer(renderer) {
    worker->Start();
    for (size_t i = 0; i < track_count; i++) {
      webrtc::scoped_refptr<webrtc::VideoTrack> track;
      sources.push_back(CreateTrackAndAddSink(renderer, worker.get(), &track));
      tracks.push_back(track);
    }
  }

  ~TrackFixture() {
    StopInjection();
    worker->BlockingCall([&] {
      for (const webrtc::scoped_refptr<webrtc::VideoTrack>& track : tracks) {
        renderer.RemoveTrack(track.get());
      }
    });
    tracks.clear();
  }

  // 指定した寸法・回転のフレームを 30fps で注入し続ける
  // 枠割りの再計算 (SetOutlines) はフレーム受信後に非同期で走るため、
  // 1 回だけの注入では Sink が「枠変更中」のままスキップされ続ける。
  // 実カメラのように継続注入することで、再計算後の描画が必ず観測できる
  // 全 Sink に同じ寸法を注入し、グリッド上のセルごとの差だけを検証できるようにする
  void StartInjection(int width, int height, webrtc::VideoRotation rotation) {
    StopInjection();
    injecting_.store(true);
    injection_thread_ = std::thread([this, width, height, rotation]() {
      while (injecting_.load()) {
        for (const webrtc::scoped_refptr<TestVideoSource>& source : sources) {
          source->InjectFrame(width, height, rotation);
        }
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

// 同一行の隣接セルが隙間なく連続していることを確認する
// cols 列のグリッドでは i 番目のセルの右端と i+1 番目のセルの左端が一致する
bool RowsAreContiguous(const std::vector<ActualRect>& actual, int cols) {
  for (size_t i = 0; i + 1 < actual.size(); i++) {
    if (static_cast<int>(i) % cols != cols - 1) {
      if (actual[i + 1].offset_x != actual[i].offset_x + actual[i].width) {
        return false;
      }
    }
  }
  return true;
}

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

TEST_CASE("BaseRenderer が 16:9 ウィンドウに 2 つの映像を横並びで配置する",
          "[base_renderer]") {
  TestRenderer renderer(2560, 1440);
  TrackFixture fixture(renderer, 2);

  // 16:9 の映像を 16:9 のウィンドウに 2 つ並べると cols=2 / rows=1 になり、
  // 各セルはウィンドウを左右に等分割した 1280x720 になる
  // 上下に残る 360 px ずつはウィンドウ中央寄せの offset になる
  fixture.StartInjection(1920, 1080, webrtc::kVideoRotation_0);
  std::vector<ExpectedRect> expected =
      std::vector<ExpectedRect>({{0, 360, 1280, 720}, {1280, 360, 1280, 720}});
  bool matched = renderer.WaitForSinkRects(expected);
  std::vector<ActualRect> actual = renderer.GetActualRects();
  CAPTURE(RectToActualString(actual));
  REQUIRE(matched);
  REQUIRE(renderer.RegionHasVideo(0, 360, 1280, 720));
  REQUIRE(renderer.RegionHasVideo(1280, 360, 1280, 720));
  // 左右に並ぶ 2 つのセルが隙間なく連続すること
  REQUIRE(RowsAreContiguous(actual, 2));
}

TEST_CASE("BaseRenderer が 2 つの映像のアスペクトで枠割りを決める",
          "[base_renderer]") {
  TestRenderer renderer(2560, 1440);
  TrackFixture fixture(renderer, 2);

  // 4:3 の映像を 16:9 のウィンドウに 2 つ並べると、代表 Sink の実測アスペクト
  // 4:3 が採用されて各セルは 1280x960 になる
  // 上下に残る 240 px ずつはウィンドウ中央寄せの offset になる
  fixture.StartInjection(640, 480, webrtc::kVideoRotation_0);
  std::vector<ExpectedRect> expected =
      std::vector<ExpectedRect>({{0, 240, 1280, 960}, {1280, 240, 1280, 960}});
  bool matched = renderer.WaitForSinkRects(expected);
  std::vector<ActualRect> actual = renderer.GetActualRects();
  CAPTURE(RectToActualString(actual));
  REQUIRE(matched);
  REQUIRE(RowsAreContiguous(actual, 2));
}

TEST_CASE("BaseRenderer が 4 つの映像を 2x2 のグリッドに配置する",
          "[base_renderer]") {
  TestRenderer renderer(2560, 1440);
  TrackFixture fixture(renderer, 4);

  // 4 つの映像では cols=2 / rows=2 になり、3 番目以降のセルは 2 行目の先頭から
  // cell 座標の累積式で配置される
  // 理想枠がウィンドウをちょうど覆うため offset は 0 になる
  fixture.StartInjection(1920, 1080, webrtc::kVideoRotation_0);
  std::vector<ExpectedRect> expected =
      std::vector<ExpectedRect>({{0, 0, 1280, 720},
                                 {1280, 0, 1280, 720},
                                 {0, 720, 1280, 720},
                                 {1280, 720, 1280, 720}});
  bool matched = renderer.WaitForSinkRects(expected);
  std::vector<ActualRect> actual = renderer.GetActualRects();
  CAPTURE(RectToActualString(actual));
  REQUIRE(matched);
  // 上段と下段の両方で、左右に並ぶセルが隙間なく連続すること
  REQUIRE(RowsAreContiguous(actual, 2));
}

TEST_CASE("BaseRenderer が縦長ウィンドウで 2 つの映像を縦並びに配置する",
          "[base_renderer]") {
  TestRenderer renderer(360, 640);
  TrackFixture fixture(renderer, 2);

  // ウィンドウ (9:16) が映像 (16:9) より縦長なので、cols=1 / rows=2 の 1 列に並ぶ
  // 各セルはウィンドウを上下に等分割した 360x320 の枠に収まると判断され、
  // 映像アスペクト 16:9 を保つ 360x202 になる。
  // 2 つ目のセルはセル右端の累積計算 (grid_offset_x_f + 2 * 理想枠幅) の
  // float 丸めで右端が 1 px 手前になり 359 px になる
  // 理想枠の合計がウィンドウを覆わないため、上下に残る 117 px と 320 px が
  // ウィンドウ中央寄せの offset になる
  fixture.StartInjection(1920, 1080, webrtc::kVideoRotation_0);
  std::vector<ExpectedRect> expected =
      std::vector<ExpectedRect>({{0, 117, 360, 202}, {0, 320, 359, 202}});
  bool matched = renderer.WaitForSinkRects(expected);
  std::vector<ActualRect> actual = renderer.GetActualRects();
  CAPTURE(RectToActualString(actual));
  REQUIRE(matched);
  REQUIRE(AllRectsInsideWindow(actual, renderer.CanvasWidth(),
                               renderer.CanvasHeight()));
}

TEST_CASE("BaseRenderer が極小ウィンドウで 0 寸法の枠になっても abort しない",
          "[base_renderer]") {
  TestRenderer renderer(1, 1);
  TrackFixture fixture(renderer, 2);

  // 極小ウィンドウに 2 つの映像を入れると、グリッドは cols=1 / rows=2 になるが
  // 理想枠が 1x1 を下回るため、両方のセルが幅 0 に潰れる
  // (1 つ目は 0x0、2 つ目は高さ 1 px の枠になるが、幅 0 の枠は SinkInfo に
  // 含めず描画しない設計なので SinkInfo は空のままになる)
  // この状態でクラッシュせず描画ループが回り続けること (0 寸法の枠を
  // 描画バッファへ書き込まないこと) を検証する
  fixture.StartInjection(1920, 1080, webrtc::kVideoRotation_0);
  // 0 寸法の枠しかない状態で描画スレッドが落ちないことを確認する
  // Render() は SinkInfo が空のまま呼ばれ続ける
  bool observed = renderer.WaitUntil(
      [](const std::vector<ActualRect>& actual) { return actual.empty(); });
  std::vector<ActualRect> actual = renderer.GetActualRects();
  CAPTURE(RectToActualString(actual));
  REQUIRE(observed);
  REQUIRE(actual.empty());
  REQUIRE(renderer.CanvasWidth() == 1);
  REQUIRE(renderer.CanvasHeight() == 1);

  // ウィンドウを戻すと 2 つの映像が再び描画対象になること
  // 描画スレッドが 0 寸法の状態から復帰できる (落ちていない) ことを確認する
  renderer.SetSize(2560, 1440);
  std::vector<ExpectedRect> expected =
      std::vector<ExpectedRect>({{0, 360, 1280, 720}, {1280, 360, 1280, 720}});
  bool recovered = renderer.WaitForSinkRects(expected);
  std::vector<ActualRect> recovered_actual = renderer.GetActualRects();
  CAPTURE(RectToActualString(recovered_actual));
  REQUIRE(recovered);
  REQUIRE(!recovered_actual.empty());
}
