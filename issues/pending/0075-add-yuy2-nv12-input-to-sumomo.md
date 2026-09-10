# sumomo に YUY2 / NV12 のカメラ入力対応を追加する

- Created: 2026-09-10
- Completed: {YYYY-MM-DD}
- Branch: feature/add-yuy2-nv12-input-to-sumomo
- Polished: {YYYY-MM-DD}

## 目的

sumomo で YUY2 (4:2:2) や NV12 を出力するカメラを利用できるようにする。Arducam などの YUY2 出力カメラで Intel VPL + H.265 の 720p 120fps 配信を検証できるようにするため。momo では `--force-yuy2` / `--force-nv12` として対応済みであり、同等の機能を sumomo に移植する。

## 現状

- momo では PR #395 (Feature/yuy2) と PR #398 (`--force-nv12` オプションを追加する) で YUY2 / NV12 のカメラ入力に対応し、`--force-yuy2` / `--framerate 120` で Intel VPL + H.265 の 120fps 配信を確認済み
- sora-cpp-sdk には momo の V4L2 側の変更が取り込まれている (`Momo で変更した部分を反映する` のコミット)
  - `include/sora/v4l2/v4l2_video_capturer.h` の `V4L2VideoCapturerConfig` には `force_i420` / `force_yuy2` / `force_nv12` / `use_native` がある
  - `src/v4l2/v4l2_video_capturer.cpp` は `force_yuy2` 指定時に `V4L2_PIX_FMT_YUYV` を選択し、取得した YUY2 を libyuv の `YUY2ToNV12` で NV12 に変換する。NV12 は変換せずそのまま利用する
- 一方、sumomo が使う `sora::CreateCameraDeviceCapturer` の `CameraDeviceCapturerConfig` (`include/sora/camera_device_capturer.h`) には `force_i420` と `use_native` しかなく、`force_yuy2` / `force_nv12` がない。`src/camera_device_capturer.cpp` も Linux / NVCODEC の各分岐で `V4L2VideoCapturerConfig` へ `force_yuy2` / `force_nv12` を伝播していない
- sumomo の `SumomoConfig` と CLI11 定義に `--force-yuy2` / `--force-nv12` はない。momo の `--force-yuy2` / `--force-nv12` は V4L2 キャプチャーで指定フォーマットを強制し、利用できない場合はエラーで終了する
- sora-cpp-sdk の VPL エンコーダー (`src/hwenc_vpl/vpl_video_encoder.cpp`) は NV12 入力をそのまま利用できる (`MFX_FOURCC_NV12`)。V4L2 側で YUY2 を NV12 に変換すれば、VPL エンコーダー側の変更は不要である
- sumomo には 120fps を指定する `--framerate` がなく、フレームレート指定の対応が別途必要である
- `examples/sumomo/README.md` に YUY2 / NV12 の記載はない

## 設計方針

- まず Intel VPL + H.265 の 720p 120fps を対象とする。AMD や AV1 は次のステップとし、本 issue の対象外とする
- `CameraDeviceCapturerConfig` に `force_yuy2` / `force_nv12` を追加し、`src/camera_device_capturer.cpp` で `V4L2VideoCapturerConfig` へ伝播する
- sumomo に momo と同じ `--force-yuy2` / `--force-nv12` を追加する。`--force-*` 系は同時指定不可とし、指定フォーマットが利用できない場合はエラーで終了する
- 120fps を指定するフレームレート指定 (`--framerate`) の対応を前提とする
- `examples/sumomo/README.md` の「映像と音声のデバイスに関するオプション」に `--force-yuy2` / `--force-nv12` を追記する
- `CHANGES.md` の `## develop` に `[ADD]` エントリを追記する

## Pending 理由

- `CameraDeviceCapturerConfig` に `force_yuy2` / `force_nv12` を公開 API として追加するか、sumomo から `sora::V4L2VideoCapturer` を直接使うか、設計判断が必要である
- momo で確認済みの 120fps が `CreateCameraDeviceCapturer` 経由の sumomo のパスでも出るかは未確認で、Arducam の YUY2 カメラと Intel VPL 環境での実機検証が必要である
- 120fps を指定するためのフレームレート指定対応が前提となる

## Pending 解除条件

- `CameraDeviceCapturerConfig` への追加か sumomo での直接利用かが確定したこと
- フレームレート指定の対応が完了したこと
- 実機 (Arducam + Intel VPL) で 720p 120fps の検証ができる段階になったこと

## 完了条件

- `--force-yuy2` を指定すると、YUY2 出力のカメラから 720p 120fps でキャプチャし、Intel VPL の H.265 エンコーダーで Sora へ配信できること
- `--force-nv12` を指定すると、NV12 出力のカメラから同様に配信できること
- `--force-yuy2` / `--force-nv12` を指定しない場合、既存のカメラ / fake の挙動が変わらないこと
- 指定フォーマットが利用できない場合はエラーで終了すること
- `examples/sumomo/README.md` と `CHANGES.md` が更新されていること

## テスト方針

- 手元の Intel VPL 環境と Arducam の YUY2 カメラで `--force-yuy2 --framerate 120 --video-codec-type H265 --h265-encoder intel_vpl` を指定して Sora へ接続し、120fps で配信されることを確認する
- NV12 出力のカメラで `--force-nv12` を指定して配信できることを確認する
- 既存の `--fake-capture-device` を利用した E2E テストが引き続き通ることを確認する
