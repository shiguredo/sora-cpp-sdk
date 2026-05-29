// デコーダファクトリの GetSupportedFormats と Create を並行呼び出ししても abort しないことを検証する
// 修正前は formats_ への未同期アクセスにより _LIBCPP_HARDENING_MODE_EXTENSIVE の境界チェックが発火して abort する
#include <atomic>
#include <chrono>
#include <thread>

#include <api/environment/environment_factory.h>
#include <api/video_codecs/sdp_video_format.h>
#include <catch2/catch_test_macros.hpp>

#include "sora/sora_video_decoder_factory.h"

TEST_CASE("SoraVideoDecoderFactory の並行アクセスで abort しないこと",
          "[data_race]") {
  auto config = sora::GetSoftwareOnlyVideoDecoderFactoryConfig();
  sora::SoraVideoDecoderFactory factory(config);
  auto env = webrtc::CreateEnvironment();
  auto formats = factory.GetSupportedFormats();
  REQUIRE(!formats.empty());

  const webrtc::SdpVideoFormat format = formats[0];

  std::atomic<bool> start{false};
  std::atomic<bool> gs_done{false};
  std::atomic<bool> create_done{false};

  auto gs_thread = std::thread([&]() {
    while (!start.load()) {
    }
    for (int i = 0; i < 10000; i++) {
      auto f = factory.GetSupportedFormats();
      if (f.empty()) {
        // abort せず空が返ってきた場合もある（明確な期待動作ではないが、空でも通ることは確認する）
      }
    }
    gs_done.store(true);
  });

  auto create_thread = std::thread([&]() {
    while (!start.load()) {
    }
    for (int i = 0; i < 10000; i++) {
      auto dec = factory.Create(env, format);
      // abort しなければ、nullptr でも OK（競合解消後に非 null を期待するかの確認はフェーズ 2 で行う）
    }
    create_done.store(true);
  });

  start.store(true);

  // 最大 30 秒待つ（どちらかが abort したらそもそもここに到達しない）
  auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
  while (!gs_done.load() || !create_done.load()) {
    if (std::chrono::steady_clock::now() > deadline) {
      break;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }

  gs_thread.join();
  create_thread.join();

  REQUIRE(gs_done.load());
  REQUIRE(create_done.load());
}
