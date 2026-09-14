# sumomo に --screen-capture を追加する

- Created: 2026-09-10
- Completed: {YYYY-MM-DD}
- Branch: feature/add-screen-capture-to-sumomo
- Polished: 2026-09-14

## 目的

sumomo でカメラデバイスの代わりに画面をキャプチャして配信できるようにする。momo の `--screen-capture` と同等の機能を提供し、カメラがない環境やデスクトップ共有の用途で sumomo を利用できるようにする。

## 現状

- sumomo の映像入力は `examples/sumomo/src/sumomo.cpp` の `Sumomo::Run()` で生成している。`config_.fake_capture_device` が true の場合は `sora::FakeVideoCapturer` を、false の場合は `sora::CreateCameraDeviceCapturer` を利用しており、画面キャプチャの経路はない
- `examples/sumomo/src/sumomo.cpp` の `SumomoConfig` と `main()` の CLI11 定義に画面キャプチャ関連のオプションはない
- `examples/sumomo/CMakeLists.txt` は `src/sumomo.cpp` と `src/sdl_renderer.cpp` のみをビルドしており、プラットフォーム別の設定はコンパイルオプション・定義の `if(WIN32)/else()` 分岐と `if(LINUX)` の `BUILD_RPATH` 設定のみで、リンク先は全プラットフォーム共通の `Sora::sora` と `SDL3::SDL3`
- `examples/sumomo/run.py` はサンプル固有の cmake オプションを渡していない
- momo には以下がある
  - `src/rtc/screen_video_capturer.{h,cpp}` の `ScreenVideoCapturer` : `webrtc::DesktopCapturer::CreateScreenCapturer` と `webrtc::DesktopAndCursorComposer` を使い、`GetSourceList` の先頭のソースをキャプチャする。`libyuv` の `ARGBScale` / `ARGBToI420` で指定解像度に収まるようスケーリングし、`sora::ScalableVideoTrackSource::OnFrame` へ I420 フレームを渡す
  - `src/util.cpp` の `--screen-capture` オプションと `is_valid_screen_capture` バリデータ : `USE_SCREEN_CAPTURER` が定義されていないビルドではエラーにする
  - `src/main.cpp` のキャプチャ生成分岐 : `args.screen_capture` が true の場合は `ScreenVideoCapturer` を使う。起動時に `GetSourceListString` でキャプチャ対象一覧をログ出力する
  - `run.py` の `USE_SCREEN_CAPTURER` の有効化 : windows_x86_64 / macos_x86_64 / macos_arm64 / ubuntu-22.04_x86_64 / ubuntu-24.04_x86_64 のみ
  - `CMakeLists.txt` のリンク設定 : Linux では Xdamage / Xfixes / Xcomposite / Xrandr、macOS では `-framework IOSurface` をリンクする
- sora-cpp-sdk が利用する prebuilt libwebrtc には `modules/desktop_capture` が含まれている。手元の `examples/_install/macos_arm64/release/webrtc/lib/libwebrtc.a` に `webrtc::DesktopCapturer` のシンボルがあることを確認済み
- `examples/sumomo/README.md` のオプション一覧に画面キャプチャの項目はない

## 設計方針

- momo の `ScreenVideoCapturer` を `examples/sumomo/src/screen_video_capturer.{h,cpp}` に移植する。`sora::ScalableVideoTrackSource` を継承し、`Sumomo::Run()` のビデオソース生成で `--screen-capture` 指定時に使う
  - 画面キャプチャはサンプル固有の要件のため、SDK 本体 (`include/sora/` / `src/`) には追加しない
  - キャプチャ対象は momo と同じく `GetSourceList` の先頭のソースとし、選択オプションは追加しない
  - フレームレートはカメラ / fake と同じ 30 fps とする
- `SumomoConfig` に `bool screen_capture = false;` を追加し、CLI11 に `--screen-capture` フラグを登録する。momo と同様に未対応プラットフォームではバリデータでエラーにする
- 対応プラットフォームは momo に合わせた Windows x86_64 / macOS arm64 / Ubuntu 22.04・24.04 x86_64 に加えて、Ubuntu 26.04 x86_64 も対応とする。armv8 / Raspberry Pi OS は prebuilt libwebrtc の desktop capture 対応状況が不明なため対象外とする
  - Ubuntu 26.04 x86_64 は sumomo の対応ターゲットに含まれる。m154.8037.1.1 の prebuilt libwebrtc に desktop capture が含まれることを 2026-09-14 に確認済み (macOS arm64 / Ubuntu 26.04 x86_64 の libwebrtc.a に `webrtc::DesktopCapturer` のシンボルあり)。momo の ScreenVideoCapturer が使う API (`CreateScreenCapturer` / `SelectSource` / `CaptureFrame` 等) は m154 のヘッダにも存在するため、対象に含める
- `examples/sumomo/CMakeLists.txt` に `USE_SCREEN_CAPTURER` オプションを追加し、有効時のみ `screen_video_capturer.cpp` を追加して、`target_compile_definitions` で `USE_SCREEN_CAPTURER` マクロを定義してからプラットフォーム別のライブラリをリンクする
  - `USE_SCREEN_CAPTURER` マクロは momo と同様に `is_valid_screen_capture` バリデータとキャプチャ生成分岐で使用する
- `examples/sumomo/run.py` で対応プラットフォームの場合に `-DUSE_SCREEN_CAPTURER=ON` を渡す
- 起動時にキャプチャ対象のソース一覧をログ出力し、どの画面が配信されるか確認できるようにする
- `examples/sumomo/README.md` の「映像と音声のデバイスに関するオプション」に `--screen-capture` を追記する
- `CHANGES.md` の `## develop` に `[ADD]` エントリを追記する

## 懸念

- macOS では画面収録の許可 (TCC) が必要で、許可されていない場合はフレームが取得できない。権限の状態によっては利用者に事前説明が必要になる
- Ubuntu で Wayland 環境の場合は WebRTC の PipeWire 経路が必要になる。m154.8037.1.1 の prebuilt libwebrtc には PipeWire が含まれていないことを 2026-09-14 に確認済み (Ubuntu 26.04 x86_64 の libwebrtc.a に pipewire のシンボル・文字列なし) のため、Wayland セッションでは画面キャプチャできない。X11 セッションで利用する必要がある

## 完了条件

- `--screen-capture` を指定して sumomo を起動すると、`--resolution` に収まるようスケーリングされた画面映像 (マウスカーソル合成あり) が Sora へ配信されること
- `--screen-capture` を指定しない場合、カメラおよび `--fake-capture-device` の既存の挙動が変わらないこと
- 未対応プラットフォームで `--screen-capture` を指定した場合、CLI のエラーとして表示されること
- `examples/sumomo/README.md` にオプションが記載されていること
- `CHANGES.md` の `## develop` に `[ADD]` エントリが追記されていること

## テスト方針

- 手元の Windows / macOS / Ubuntu デスクトップ環境で `--screen-capture` を指定して Sora へ接続し、画面映像が配信されることを確認する
- 画面キャプチャは表示環境と OS の画面収録権限が必要なため、CI (GitHub Actions) での自動テストは追加しない
- 既存の `--fake-capture-device` を利用した E2E テストが引き続き通ることを確認する
