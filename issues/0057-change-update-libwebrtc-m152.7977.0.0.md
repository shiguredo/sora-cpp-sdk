# libwebrtc を m152.7977.0.0 にあげる

- Created: 2026-08-17
- Completed: {YYYY-MM-DD}
- Branch: feature/update-m152.7977.0.0
- Polished: 2026-08-19

## 目的

libwebrtc のバージョンを m151.7922.0.0 から m152.7977.0.0 に更新し、libwebrtc のアップデートに追従する。
m152.7977.0.0 は webrtc-build 側で対応が完了しリリース済みのため、sora-cpp-sdk も追従する。

## 影響範囲

### libwebrtc 側の変更（m152）

m152 で `rtc_media_base` ビルドターゲットが削除され、`media/base` 側の互換ラッパーヘッダが削除された。以下は実体が m151 時点で既に `api/video` に移動していたヘッダであり、`media/base` への include パスは m152 で使えなくなる。

- `adapted_video_track_source` : sora-cpp-sdk が include しているため、include パスの更新が必要
- `video_common` : 同上

### sora-cpp-sdk への影響

ヘッダ移動の影響を受ける include は以下の 5 箇所。いずれも `media/base/` → `api/video/` の変更。

- `include/sora/scalable_track_source.h` : `ScalableVideoTrackSource` が `webrtc::AdaptedVideoTrackSource` を継承しており、`adapted_video_track_source.h` の include パスを更新する
- `src/scalable_track_source.cpp` : 同上
- `src/v4l2/v4l2_device.cpp` : `webrtc::GetFourccName` を使用しており、`video_common.h` の include パスを更新する
- `src/v4l2/v4l2_video_capturer.cpp` : 同上
- `test/base_renderer.cpp` : `TestVideoSource` が `webrtc::AdaptedVideoTrackSource` を継承しており、`adapted_video_track_source.h` の include パスを更新する

`src/v4l2/` の 2 ファイルは `CMakeLists.txt` の `SORA_TARGET_OS STREQUAL "ubuntu"` のときのみコンパイルされる（raspberry-pi-os_armv8 も `SORA_TARGET_OS` が "ubuntu" になるためビルド対象）。macOS でのビルドではヘッダ欠落が検出されない。

`test/base_renderer.cpp` はネイティブビルドかつ `--test` 指定時に `TEST_BASE_RENDERER=ON` になり、CI で必ずコンパイル・実行される。

### 影響を受けない箇所

m152 でも存在が確認できたヘッダであり、変更不要。

- `media/base/media_constants.h`（`src/open_h264_video_encoder.cpp` / `src/default_video_formats.cpp`）
- `media/base/codec.h`（`include/sora/hwenc_v4l2/v4l2_h264_encoder.h` / `src/hwenc_v4l2/v4l2_h264_encoder.cpp`。`USE_V4L2_ENCODER=ON` かつ `SORA_TARGET_OS STREQUAL "ubuntu"` のときのみビルドされる）
- `media/engine/simulcast_encoder_adapter.h`（`src/sora_video_encoder_factory.cpp`）

`video_adapter` / `video_broadcaster` も `api/video` へ移動しているが、sora-cpp-sdk では使用していないため影響なし。

## 実装内容

1. `DEPS` と `examples/DEPS` の `WEBRTC_BUILD_VERSION` を m151.7922.0.0 から m152.7977.0.0 に更新する
   - `run.py` の `check_version_file` 関数が DEPS と examples/DEPS の `WEBRTC_BUILD_VERSION` の一致をチェックするため、両方更新する
2. あらかじめ webrtc-build の `feature/m152.7977` ブランチで `api/video/adapted_video_track_source.h` のコンストラクタ等の API シグネチャが m151 / m152 で変更されていないことを確認した上で、影響範囲に記載した 5 ファイルの include パスを更新する
3. `CHANGES.md` の `## develop` に `[UPDATE] libwebrtc のバージョンを m152.7977.0.0 に上げる`、`### misc` に `[UPDATE] Examples の WEBRTC_BUILD_VERSION を m152.7977.0.0 にあげる` のエントリを追記する
   - 担当者ハンドル `@<担当者>` は PR 作成者のものに書き換える

## 検証内容

- ローカルで sumomo の E2E テストを実行する
  - 事前に `python3 run.py build <target>` で変更後の sora-cpp-sdk をビルドし、`python3 examples/sumomo/run.py build <target> --local-sora-cpp-sdk-dir <sora-cpp-sdk のパス>` で sumomo をビルドしておくこと
    - `--local-sora-cpp-sdk-dir` を指定しないとリリース版 SDK（m152 の変更を含まない）がダウンロードされ、m152 の変更が検証されないため必須
  - `TEST_SIGNALING_URL` / `TEST_CHANNEL_ID_PREFIX` / `TEST_SECRET_KEY` の環境変数を設定しておくこと
  - `uv run --directory=e2e-test pytest test_sumomo_basic.py::test_sumomo_sendonly_recvonly[VP8] -v -s --timeout=60` のように特定ケースのみ実行する
- iOS は `test/ios` の Xcode プロジェクトを実機でビルド・起動し、WSS 接続できることを確認する
  - 事前に `python3 run.py build ios` で SDK をビルドしておくこと
  - シグナリング URL とチャンネル ID（`test/ios/hello/ViewController.mm` 内）は実装者の接続先に書き換えること
- Android は `test/android` の Gradle プロジェクトを実機でビルド・起動し、WSS 接続できることを確認する
  - 事前に `python3 run.py build android` で SDK をビルドしておくこと
  - シグナリング URL とチャンネル ID（`test/android/app/src/main/cpp/native-lib.cpp` 内）は実装者の接続先に書き換えること
- Android の JNI エクスポート形式が m152 で変更されていないことを確認する
  - android ビルドは `run.py` が `linux-x86_64` の NDK パスを前提としているため、Linux 環境で実施すること
  - android ビルド後、android-ndk 内の `llvm-readelf`（`_install/android/release/android-ndk/toolchains/llvm/prebuilt/<host>/bin/llvm-readelf`）で `llvm-readelf -Ws _install/android/release/webrtc/lib/arm64-v8a/libwebrtc.a | grep Java_J_N_` を実行し、`Java_J_N_` シンボルが出力されることを確認する（m151 で `Java_org_webrtc_` → `Java_J_N_` の変更があったため）
  - 形式が再変更されていた場合は、0056 と同様に `run.py` の JNI シンボル保持対象を更新する

## 完了条件

- `feature/update-m152.7977.0.0` ブランチの push で CI が成功していること
  - ビルドジョブ（windows / macos / ios / ubuntu / raspberry-pi-os / android）が成功していること
  - Android / iOS 以外のプラットフォームでは、GitHub-hosted ランナーの sumomo の E2E テスト（pytest）が成功していること
    - self-hosted ランナーの HWA 系テスト（apple_video_toolbox / nvidia_video_codec / intel_vpl / device）は対象外
    - raspberry-pi-os の E2E は 0059（V4L2 M2M エンコーダのフレームペアリングズレ）の影響で確率的に失敗するため対象外とし、0059 を先にマージした上で 0059 の完了条件（`issues/0059-...md` の `Completed` フィールド）で担保する
- iOS / Android で実機動作確認を実施し、問題がないことを確認していること
- `feature/update-m152.7977.0.0` の内容が develop にマージされていること
- develop の `DEPS` と `examples/DEPS` の `WEBRTC_BUILD_VERSION` が `m152.7977.0.0` になっていること
- `CHANGES.md` の `## develop` に `[UPDATE] libwebrtc のバージョンを m152.7977.0.0 に上げる`、`### misc` に `[UPDATE] Examples の WEBRTC_BUILD_VERSION を m152.7977.0.0 にあげる` のエントリが追加されていること
- Android の JNI エクスポート形式が m152 で変更されていないことを確認していること
