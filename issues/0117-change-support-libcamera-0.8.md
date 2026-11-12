# Raspberry Pi OS 向けビルドを libcamera 0.8.0 に追従させる

- Created: 2026-11-12
- Completed: {YYYY-MM-DD}
- Branch: feature/change-support-libcamera-0.8
- Polished: {YYYY-MM-DD}

## 目的

Raspberry Pi の apt リポジトリに libcamera 0.8.0 が配布されたため、本 SDK の Raspberry Pi OS 向けビルドを 0.8.0 に追従させる。libcamera は soname で ABI が分かれており (`libcamera.so.0.7` → `libcamera.so.0.8`)、0.8.0 を入れた端末では 0.7 系のヘッダでビルドした `libcamerac.so` を読み込めない。追従を遅らせるほど、Raspberry Pi OS を更新した利用者との組み合わせで `libcamera` を利用したカメラキャプチャが動かなくなる期間が長くなる。

## 現状

### 配布状況 (2026-11-12 に確認)

- `archive.raspberrypi.com` の `dists/trixie/main/binary-arm64/Packages` に `libcamera-dev` **0.8.0+rpt20261104-1** が追加されている
  - `Depends: libcamera0.8 (= 0.8.0+rpt20261104-1)`
  - `Filename: pool/main/libc/libcamera/libcamera-dev_0.8.0+rpt20261104-1_arm64.deb`
- ランタイムパッケージは `libcamera0.7` から `libcamera0.8` に変わり、deb 内のシンボリックリンクは `libcamera.so -> libcamera.so.0.8`、`libcamera-base.so -> libcamera-base.so.0.8` になる
- `libcamera.pc` / `libcamera-base.pc` の `Version` は `0.8.0`
- `libcamera0.8` は新たに `libpisp1 (>= 1.8.0)` と `libcamera-ipa (= 0.8.0+rpt20261104-1)` に依存する (apt の依存解決で sysroot に入るため、追加のパッケージ指定は不要)
- `libcamera-ipa` には `ipa_rpi_vc4.so` / `ipa_rpi_pisp.so` / `ipa_vimc.so` が含まれており、実行時に libcamera がモジュールとして読み込む

### sysroot の取得方法

- `sysroot/raspberry-pi-os_armv8.json` はパッケージ名 `libcamera-dev` だけを指定しており、バージョンはリポジトリの最新が解決される
- `buildbase.py` の `build_sysroot()` は `sysroot_config_fingerprint()` が一致すれば既存の sysroot を再利用するため、**apt 側のバージョンが上がっただけでは sysroot は作り直されない**
- `run.py` の `build` サブコマンドには `--rootfs-fetch-force` (変数名 `rootfs_fetch_force`) があり、これで sysroot を強制的に作り直せる

### 本 SDK 側の libcamera 利用箇所

- `third_party/libcamerac/libcamerac.cpp` が libcamera の公開 API を C ABI でラップしている (`CameraManager` / `Camera` / `CameraConfiguration` / `StreamConfiguration` / `FrameBufferAllocator` / `ControlList` / `Request` など)
- `third_party/libcamerac/libcamerac.h` と `third_party/libcamerac/libcameracpp.h` が C API / C++ ラッパのヘッダ
- `src/hwenc_v4l2/libcamera_capturer.cpp` が `libcamerac` を利用したキャプチャラ
- `CMakeLists.txt` の `USE_V4L2_ENCODER` 分岐で `camerac` 共有ライブラリをビルドし、`camera` / `camera-base` にリンクしている。C++ 標準は libcamera 0.7.2 以降の `std::span` 利用に合わせて C++20 を指定済み
- `parse_enum_control()` (libcamerac.cpp) は `AfMode` / `AwbMode` などの値を自前の文字列テーブルで数値に変換している
- libcamera のヘッダを直接インクルードしているのは `third_party/libcamerac/libcamerac.cpp` のみで、`base/span.h` を含む 8 つのヘッダを明示的に include している

### 0.7.2+rpt20261009 から 0.8.0+rpt20261104 の差分 (タグ比較で確認)

パブリック API に関わる変更は次の 2 点。いずれも本 SDK では未使用。

- `libcamera::orientationFromRotation()` / `libcamera::transformFromRotation()` が出力引数 `bool *success` をやめて `std::optional<Orientation>` / `std::optional<Transform>` を返すようになった。本 SDK はこれらの関数を利用していない
- コントロールの enum 名から共通接頭辞が削除された (`AeMeteringMode::MeteringSpot` → `AeMeteringModeSpot`、`AwbMode::AwbAuto` → `AwbModeAuto` など)。本 SDK は libcamera の enum シンボルを参照しておらず、`parse_enum_control()` の自前テーブルと利用者から渡される文字列 (`{"AfMode", "Continuous"}` など) には影響しない。`libcamera-ipa` の rpi/vc4 チューニングファイルにも旧 enum 名の記述はない

その他、`camera.h` / `controls.h` / `request.h` / `framebuffer.h` / `framebuffer_allocator.h` / `stream.h` / `camera_manager.h` などは 0.7.2 と同一内容で、deb 同梱ヘッダは GitHub のタグ版と一致している。

### 実行時の制約

- `libcamerac.so` は libcamera の soname (`libcamera.so.0.8`) に依存するため、0.8.0 でビルドしたものは 0.7 系しか入っていない端末では読み込めない
- CI の E2E テストは self-hosted の Raspberry Pi 実機で実行される (`.github/workflows/ci.yml` の `raspberry_pi`)。この実機の libcamera も 0.8 系に更新されている必要がある

## 設計方針

- ソースコードの変更は原則不要と見込む。0.8.0 の API 差分 (上記 2 点) は本 SDK が利用していないため、まず sysroot を 0.8.0 で作り直してビルドが通ることを確認する
- sysroot は設定を変えない限り再利用されるため、追従は「`--rootfs-fetch-force` で sysroot を作り直して 0.8.0 でビルドできることを確認する」作業として扱う。バージョン固定の仕組みの追加はこの issue では行わない
- ビルドが通らない場合に限り、`third_party/libcamerac/` の実装と `CMakeLists.txt` の `camerac` ターゲットを 0.8.0 に合わせて修正する
- ラズパイ実機の libcamera を 0.8 に更新できない場合は、0.8.0 でビルドした成果物を実機で動かせないため、その状態を記録して判断を仰ぐ
- 追随した内容は `CHANGES.md` の `develop` に `[CHANGE]` または `[UPDATE]` で記載する (カテゴリの判定は `shiguredo-changelog` の規約に従う)

## 完了条件

- `python3 run.py build raspberry-pi-os_armv8 --rootfs-fetch-force` で sysroot が `libcamera-dev` 0.8.0+rpt20261104-1 と `libcamera0.8` で生成され、`camerac` と `sora` のビルドが成功すること
- 生成された `_install/raspberry-pi-os_armv8/<config>/rootfs/.webrtc-build-sysroot.json` の `deb_files` に `libcamera-dev_0.8.0+rpt20261104-1_arm64.deb` と `libcamera0.8_0.8.0+rpt20261104-1_arm64.deb` が含まれること
- `libcamerac.so` が `libcamera.so.0.8` にリンクされていること
- サンプル (`python3 examples/sumomo/run.py build raspberry-pi-os_armv8`) も同様にビルドできること。サンプル側の sysroot 設定 `examples/sysroot/raspberry-pi-os_armv8.json` は SDK 側と同一のため、同じ `--rootfs-fetch-force` が必要になる
- libcamera を利用したキャプチャが動作すること (ラズパイ実機の libcamera が 0.8 に更新されている場合)
- `CHANGES.md` に追従内容が記載されていること

## 解決方法

1. ラズパイ実機 (CI の self-hosted runner) の `libcamera0.x` のバージョンを確認する。0.7 系のままなら、先に実機を更新するか、実機なしで進めるかを決める
2. `python3 run.py build raspberry-pi-os_armv8 --rootfs-fetch-force` を実行し、sysroot の生成とビルドを確認する
3. `.webrtc-build-sysroot.json` の `deb_files`、`readelf -d` などで `libcamerac.so` の `NEEDED` が `libcamera.so.0.8` になっていることを確認する
4. ビルドが失敗した場合は、エラー内容に応じて `third_party/libcamerac/libcamerac.cpp` と `third_party/libcamerac/libcamerac.h` (必要なら `CMakeLists.txt` の `camerac` ターゲット) を修正する
5. `python3 examples/sumomo/run.py build raspberry-pi-os_armv8 --rootfs-fetch-force` でサンプルも確認する
6. ラズパイ実機の E2E テスト (`e2e-test/test_sumomo_raspberry_pi.py`) を実行し、libcamera を利用した送受信が動くことを確認する
7. `CHANGES.md` の `develop` に追従内容を記載する
