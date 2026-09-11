# HWA の使い方ドキュメントを追加する

- Created: 2026-09-11
- Completed: {YYYY-MM-DD}
- Branch: feature/update-hwa-documentation
- Polished: {YYYY-MM-DD}

## 目的

Sora C++ SDK は 2025.2.0 で `SoraClientContextConfig` から `use_hardware_encoder` を削除し、デフォルトで libwebrtc 内蔵のエンコーダー / デコーダーを利用するようになった。HWA (ハードウェアアクセラレーション) の利用は C++ SDK の重要な機能であるため、`SoraClientContextConfig::video_codec_factory_config` で HWA を有効にする方法を `doc/` 以下に追加する。

## 現状

- `doc/` に HWA の使い方を説明したドキュメントはない (`doc/faq.md` に部分的な言及はある)
- `CHANGES.md` の 2025.2.0 に `[CHANGE] SoraClientContextConfig から use_hardware_encoder を削除` と、`SoraClientContextConfig::video_codec_factory_config` を適切に設定する必要がある旨が書かれているが、具体的な設定方法はドキュメント化されていない
- 設定に使う API は `include/sora/` に存在する
  - `SoraClientContextConfig::video_codec_factory_config` (型は `SoraVideoCodecFactoryConfig`)
  - `SoraVideoCodecFactoryConfig::preference` (`std::optional<VideoCodecPreference>`)
  - `SoraVideoCodecFactoryConfig::capability_config` (`VideoCodecCapabilityConfig`)
  - `VideoCodecImplementation` (`kInternal` / `kCiscoOpenH264` / `kIntelVpl` / `kNvidiaVideoCodec` / `kAmdAmf` / `kRaspiV4L2M2M` / `kCustom_*`)
  - `GetVideoCodecCapability` / `CreateVideoCodecPreferenceFromImplementation` / `VideoCodecPreference::Merge`
  - `VideoCodecCapabilityConfig` の `cuda_context` / `amf_context` / `openh264_path` / `jni_env` / `get_custom_engines`
- HWA を使う設定方法は次の 3 パターンがある
  1. libwebrtc 内蔵を使う場合は設定不要 (`preference` が `std::nullopt` のとき `kInternal` が使われる)
  2. 環境の HWA とコーデックが分かっている場合は `VideoCodecPreference::Codec` に実装を直接指定する
  3. 環境が固定されない場合は `GetVideoCodecCapability` で利用可能な実装を取得し、`CreateVideoCodecPreferenceFromImplementation` と `VideoCodecPreference::Merge` で設定する

## 設計方針

- `doc/` に HWA の使い方を説明するドキュメントを追加する
- 次の内容を記載する
  - デフォルト (libwebrtc 内蔵) の挙動
  - `VideoCodecPreference` に実装を直接指定する方法
  - `GetVideoCodecCapability` で利用可能な実装を取得して `VideoCodecPreference` を組み立てる方法
  - `CudaContext` / `AMFContext` など、HWA に必要なコンテキストの設定
  - `VideoCodecImplementation` の一覧
  - `VideoCodecPreference::Merge` の上書きの挙動
- 実装の変化に追従できるよう、`include/sora/` の API を参照する形で書く

## 完了条件

- `doc/` に HWA の使い方を説明したドキュメントが追加されていること
- libwebrtc 内蔵 / HWA を明示指定 / capability から組み立てる 3 パターンの設定方法が記載されていること
- `VideoCodecImplementation` の一覧と、HWA に必要なコンテキストの設定が記載されていること
