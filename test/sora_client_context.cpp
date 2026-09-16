// SoraClientContextConfig::field_trials が libwebrtc の Environment に反映されることを検証する
// SoraClientContext の生成には実際のオーディオデバイスが必要ないため、
// use_audio_device を false にしてダミー ADM を利用する
#include <memory>
#include <string>
#include <vector>

// WebRTC
#include <api/environment/environment.h>
#include <api/peer_connection_interface.h>

// Catch2
#include <catch2/catch_test_macros.hpp>

// Sora C++ SDK
#include <sora/sora_client_context.h>

namespace {

// 実際のオーディオデバイスを使わない SoraClientContextConfig を返す
sora::SoraClientContextConfig CreateContextConfig() {
  sora::SoraClientContextConfig config;
  config.use_audio_device = false;
  return config;
}

}  // namespace

TEST_CASE(
    "field_trials を指定すると PeerConnectionFactoryDependencies::env "
    "で有効になること",
    "[sora_client_context]") {
  auto config = CreateContextConfig();
  config.field_trials = "WebRTC-Video-PerSsrcKeyframes/Enabled/";

  // configure_dependencies に渡される Environment を確認する
  bool called = false;
  bool env_has_value = false;
  bool field_trials_enabled = false;
  config.configure_dependencies =
      [&](webrtc::PeerConnectionFactoryDependencies& dependencies) {
        called = true;
        env_has_value = dependencies.env.has_value();
        if (!env_has_value) {
          return;
        }
        field_trials_enabled = dependencies.env->field_trials().IsEnabled(
            "WebRTC-Video-PerSsrcKeyframes");
      };

  auto context = sora::SoraClientContext::Create(config);

  REQUIRE(context != nullptr);
  REQUIRE(called);
  REQUIRE(env_has_value);
  REQUIRE(field_trials_enabled);
}

TEST_CASE("field_trials に不正な文字列を指定すると Create が失敗すること",
          "[sora_client_context]") {
  // libwebrtc の FieldTrials と同じく、以下を不正な文字列として扱う
  std::vector<std::string> invalid_field_trials = {
      // 末尾の '/' が無い
      "WebRTC-Video-PerSsrcKeyframes/Enabled",
      // 値が空
      "WebRTC-Video-PerSsrcKeyframes/",
      // 同じキーに異なる値が指定されている
      "WebRTC-Video-PerSsrcKeyframes/Enabled/"
      "WebRTC-Video-PerSsrcKeyframes/Disabled/",
      // キーが空
      "//",
  };

  for (const auto& field_trials : invalid_field_trials) {
    auto config = CreateContextConfig();
    config.field_trials = field_trials;

    auto context = sora::SoraClientContext::Create(config);

    INFO("field_trials: " << field_trials);
    REQUIRE(context == nullptr);
  }
}

TEST_CASE("field_trials に同じキーと値が重複していても Create が成功すること",
          "[sora_client_context]") {
  auto config = CreateContextConfig();
  config.field_trials =
      "WebRTC-Video-PerSsrcKeyframes/Enabled/"
      "WebRTC-Video-PerSsrcKeyframes/Enabled/";

  auto context = sora::SoraClientContext::Create(config);

  REQUIRE(context != nullptr);
}

TEST_CASE("field_trials が空文字の場合はフィールドトライアルを設定しないこと",
          "[sora_client_context]") {
  auto config = CreateContextConfig();
  REQUIRE(config.field_trials.empty());

  bool called = false;
  bool env_has_value = false;
  bool field_trials_enabled = true;
  config.configure_dependencies =
      [&](webrtc::PeerConnectionFactoryDependencies& dependencies) {
        called = true;
        env_has_value = dependencies.env.has_value();
        if (!env_has_value) {
          return;
        }
        // フィールドトライアルを指定していないので有効になっていない
        field_trials_enabled = dependencies.env->field_trials().IsEnabled(
            "WebRTC-Video-PerSsrcKeyframes");
      };

  auto context = sora::SoraClientContext::Create(config);

  REQUIRE(context != nullptr);
  REQUIRE(called);
  REQUIRE(env_has_value);
  REQUIRE_FALSE(field_trials_enabled);
}
