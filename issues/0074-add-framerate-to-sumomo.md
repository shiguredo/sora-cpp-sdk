# sumomo に --framerate を追加する

- Created: 2026-09-10
- Completed: {YYYY-MM-DD}
- Branch: feature/add-framerate-to-sumomo
- Polished: 2026-09-15

## 目的

sumomo でカメラ / fake デバイスのフレームレートを指定できるようにする。momo の `--framerate` と同等の機能を提供し、120 fps のような高フレームレートでの配信を検証できるようにするため。

## 現状

- `examples/sumomo/src/sumomo.cpp` の `Sumomo::Run()` は、`--fake-capture-device` 指定時に `sora::FakeVideoCapturerConfig::fps` を 30 に固定し、カメラ利用時も `sora::CameraDeviceCapturerConfig::fps` を 30 に固定している。フレームレートを変更する手段はない
- `SumomoConfig` と `main()` の CLI11 定義にフレームレート関連のオプションはない。`--resolution` は解像度のみを指定する
- SDK 側は `include/sora/capturer/fake_video_capturer.h` の `FakeVideoCapturerConfig::fps` と `include/sora/camera_device_capturer.h` の `CameraDeviceCapturerConfig::fps` で任意の値を受け取れる
- momo には `--framerate` オプション (`src/util.cpp`、デフォルト 30、`CLI::Range(1, 120)`) があり、カメラや fake デバイスの fps に使われている
- SDL / Sixel / ANSI レンダラーの表示 fps は別の仕組みで、本 issue の対象ではない
- `examples/sumomo/README.md` の「Sumomo 実行に関するオプション」にフレームレートの記載はない

## 設計方針

- `SumomoConfig` に `int framerate = 30;` を追加し、CLI11 に momo と同じ `--framerate` を登録する
- `Sumomo::Run()` の `fake_config.fps` と `cam_config.fps` に `config_.framerate` を設定する
- バリデーションは momo と合わせて `CLI::Range(1, 120)` とし、120 fps を指定可能にする
- デフォルトは momo と同じ 30 とし、既存の挙動を変えない
- `examples/sumomo/README.md` の「Sumomo 実行に関するオプション」に `--framerate` を追記する
- `CHANGES.md` の `## develop` に `[ADD]` エントリを追記する

## 完了条件

- `--framerate 120` を指定して sumomo を起動すると、カメラ / fake デバイスが 120 fps でキャプチャして送信すること
- `--framerate` を指定しない場合は従来どおり 30 fps で動作し、既存の挙動が変わらないこと
- `examples/sumomo/README.md` にオプションが記載されていること
- `CHANGES.md` の `## develop` に `[ADD]` エントリが追記されていること

## テスト方針

- 手元環境で `--framerate 120` を指定して Sora へ接続し、送信フレームレートが 120 fps 相当になることを確認する
- fake デバイス (`--fake-capture-device`) と実カメラの両方で指定できることを確認する
- 既存の `--fake-capture-device` を利用した E2E テストが引き続き通ることを確認する
