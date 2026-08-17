# libwebrtc を m152.7977.0.0 にあげる

- Created: 2026-08-17
- Completed: {YYYY-MM-DD}
- Branch: feature/update-m152.7977.0.0
- Polished: {YYYY-MM-DD}

## 目的

libwebrtc のバージョンを m151.7922.0.0 から m152.7977.0.0 に更新し、libwebrtc のアップデートに追従する。

## 現状

- develop の `DEPS` と `examples/DEPS` の `WEBRTC_BUILD_VERSION` は m150.7871.3.1
- m151.7922.0.0 への更新は `feature/update-m151.7922.0.0` ブランチで実装済みだが、develop へのマージは未実施
- webrtc-build では `feature/m152.7977` ブランチで m152 対応が完了しており、m152.7977.0.0 がリリース済み

## 影響範囲

### libwebrtc 側の変更 (m152)

m152 で `rtc_media_base` ビルドターゲットが削除されたことに伴い、以下のヘッダとビルドターゲットが `media/base` から `api/video` に移動している。

- `adapted_video_track_source`
- `video_adapter`
- `video_broadcaster`
- `video_common`

また、`stats` と `pc:libjingle_peerconnection` deps が削除された。

これらの対応は webrtc-build 側 (m152.7977.0.0) で完了済みで、sora-cpp-sdk は `WEBRTC_BUILD_VERSION` を更新するだけで取り込める。

- `add_deps.patch` / `windows_add_deps.patch` : `api/video:adapted_video_track_source` を追加
- `ios_simulcast.patch` : 依存ターゲットを `scalability_mode` と `video_codecs_api` に置き換え
- 各パッチのずれを修正

### sora-cpp-sdk への影響

ヘッダ移動の影響を受ける include は以下の 4 箇所。いずれも `media/base/` → `api/video/` の変更。

- `include/sora/scalable_track_source.h` : `ScalableVideoTrackSource` が `webrtc::AdaptedVideoTrackSource` を継承しており、`adapted_video_track_source.h` の include パスを更新する
- `src/scalable_track_source.cpp` : 同上
- `src/v4l2/v4l2_device.cpp` : `webrtc::GetFourccName` を使用しており、`video_common.h` の include パスを更新する
- `src/v4l2/v4l2_video_capturer.cpp` : 同上

`src/v4l2/` の 2 ファイルは `CMakeLists.txt` の `SORA_TARGET_OS STREQUAL "ubuntu"` のときのみコンパイルされる。macOS でのビルドではヘッダ欠落が検出されないため、Ubuntu 系ターゲットでのビルド確認が必須。

### 影響を受けない箇所

m152 でも存在が確認できたヘッダであり、変更不要。

- `media/base/media_constants.h` (`src/open_h264_video_encoder.cpp` / `src/default_video_formats.cpp`)
- `media/base/codec.h` (`include/sora/hwenc_v4l2/v4l2_h264_encoder.h` / `src/hwenc_v4l2/v4l2_h264_encoder.cpp`)
- `media/engine/simulcast_encoder_adapter.h` (`src/sora_video_encoder_factory.cpp`)

`video_adapter.h` / `video_broadcaster.h` も `api/video` へ移動しているが、sora-cpp-sdk では使用していないため影響なし。

## 実装内容

1. `DEPS` と `examples/DEPS` の `WEBRTC_BUILD_VERSION` を m151.7922.0.0 から m152.7977.0.0 に更新する
   - `run.py` の `install_deps` 関数が DEPS と examples/DEPS の `WEBRTC_BUILD_VERSION` の一致をチェックするため、両方更新する
2. 影響範囲に記載した 4 ファイルの include パスを更新する
3. 上記以外で m152 に起因するビルドエラーやリンクエラーが発生した場合は、その都度対応する
4. `CHANGES.md` に更新内容を追記する

## 検証内容

- 全ターゲットでビルドが通ること
  - 特に V4L2 を使用する Ubuntu 系ターゲット (ubuntu-22.04 / ubuntu-24.04 / ubuntu-26.04 の x86_64 / armv8) で `v4l2_device.cpp` / `v4l2_video_capturer.cpp` がビルドできること
- macOS / Windows / iOS / Android を含む全プラットフォームでビルドエラーが発生しないこと
- E2E テストでビデオの送受信が正常に動作すること

## 完了条件

- `DEPS` と `examples/DEPS` の `WEBRTC_BUILD_VERSION` が `m152.7977.0.0` になっていること
- 全プラットフォームでビルドとテストが成功していること
- `CHANGES.md` に `[UPDATE] libwebrtc のバージョンを m152.7977.0.0 に上げる` のエントリが追加されていること
