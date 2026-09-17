// エンコーダ/デコーダを生成するコールバックに webrtc::Environment が伝わることを検証する
// SoraClientContextConfig::field_trials で指定したフィールドトライアルが
// エンコーダ/デコーダにも届くために必要な経路になる
#include <memory>
#include <utility>

// WebRTC
#include <api/environment/environment.h>
#include <api/environment/environment_factory.h>
#include <api/field_trials.h>
#include <api/video/video_codec_type.h>
#include <api/video_codecs/sdp_video_format.h>
#include <api/video_codecs/video_decoder.h>
#include <api/video_codecs/video_encoder.h>

// Catch2
#include <catch2/catch_test_macros.hpp>

// Sora C++ SDK
#include <sora/sora_video_decoder_factory.h>
#include <sora/sora_video_encoder_factory.h>

namespace {

// テスト用のフィールドトライアル名
constexpr char kFieldTrialName[] = "WebRTC-Video-PerSsrcKeyframes";

// フィールドトライアル付きの webrtc::Environment を生成する
webrtc::Environment CreateEnvironmentWithFieldTrials() {
  auto field_trials =
      webrtc::FieldTrials::Create("WebRTC-Video-PerSsrcKeyframes/Enabled/");
  REQUIRE(field_trials != nullptr);

  webrtc::EnvironmentFactory env_factory;
  env_factory.Set(std::move(field_trials));
  return env_factory.Create();
}

}  // namespace

TEST_CASE("create_video_encoder に Environment が渡されること",
          "[video_factory]") {
  auto env = CreateEnvironmentWithFieldTrials();

  bool called = false;
  bool field_trials_enabled = false;
  sora::SoraVideoEncoderFactoryConfig config;
  // is_internal でない場合は SimulcastEncoderAdapter 経由で遅延して呼ばれるため、
  // Create の中で同期的に呼ばれるように is_internal を設定する
  config.is_internal = true;
  config.encoders.push_back(sora::VideoEncoderConfig(
      webrtc::kVideoCodecVP8,
      [&](const webrtc::Environment& env, const webrtc::SdpVideoFormat& format)
          -> std::unique_ptr<webrtc::VideoEncoder> {
        called = true;
        field_trials_enabled = env.field_trials().IsEnabled(kFieldTrialName);
        // コールバックが呼ばれたことだけを確認したいのでエンコーダは作らない
        return nullptr;
      }));

  sora::SoraVideoEncoderFactory factory(config);
  auto encoder = factory.Create(env, webrtc::SdpVideoFormat("VP8"));

  REQUIRE(called);
  REQUIRE(field_trials_enabled);
  // コールバックが nullptr を返した場合は nullptr が返る
  REQUIRE(encoder == nullptr);
}

TEST_CASE("create_video_decoder に Environment が渡されること",
          "[video_factory]") {
  auto env = CreateEnvironmentWithFieldTrials();

  bool called = false;
  bool field_trials_enabled = false;
  sora::SoraVideoDecoderFactoryConfig config;
  config.decoders.push_back(sora::VideoDecoderConfig(
      webrtc::kVideoCodecVP8,
      [&](const webrtc::Environment& env, const webrtc::SdpVideoFormat& format)
          -> std::unique_ptr<webrtc::VideoDecoder> {
        called = true;
        field_trials_enabled = env.field_trials().IsEnabled(kFieldTrialName);
        // コールバックが呼ばれたことだけを確認したいのでデコーダは作らない
        return nullptr;
      }));

  sora::SoraVideoDecoderFactory factory(config);
  auto decoder = factory.Create(env, webrtc::SdpVideoFormat("VP8"));

  REQUIRE(called);
  REQUIRE(field_trials_enabled);
  // コールバックが nullptr を返した場合は nullptr が返る
  REQUIRE(decoder == nullptr);
}
