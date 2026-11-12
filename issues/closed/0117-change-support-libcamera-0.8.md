# Raspberry Pi OS 向けビルドを libcamera 0.8.0 に追従させる

- Created: 2026-11-12
- Completed: 2026-11-12
- Branch: feature/change-support-libcamera-0.8
- Polished: 2026-11-12

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

- `libcamerac.so` は libcamera の soname (`SONAME` は `libcamera.so.0.8`) に依存するため、0.8.0 でビルドしたものは 0.7 系しか入っていない端末では読み込めない
- CI の E2E テストは self-hosted の Raspberry Pi 実機で実行される (`.github/workflows/ci.yml` の `raspberry_pi`)。この実機の libcamera も 0.8 系に更新されている必要がある
- 配布物とドキュメントも soname の変化に追随する必要がある
  - `.github/workflows/release.yml` はラズパイ向けの配布アーカイブに `_install/raspberry-pi-os_armv8/release/sora/lib/libcamerac.so` を同梱しており、この `libcamerac.so` が `libcamera.so.0.8` を要求するようになる
  - `doc/faq.md` の「Raspberry Pi で libcamera や libcamera control を使用したい」は、実行ファイルと同じディレクトリに `libcamera.so` を置くよう案内しており、0.8 以降の soname (`libcamera.so.0.8`) とは名前が一致しない
- CI のサンプルビルド (`.github/workflows/ci.yml` の「Build Examples (Raspberry Pi OS)」) とリリースビルドのサンプル生成は `examples/<sample>/run.py build raspberry-pi-os_armv8 --local-sora-cpp-sdk-dir ..` を実行しており、`--rootfs-fetch-force` を付けていない。サンプル側の sysroot は SDK 側と別ディレクトリ (`examples/_install/...`) に作られるため、既存のサンプル sysroot が残っていると fingerprint が一致して 0.7.2 のまま再利用される

## 設計方針

- ソースコードの変更は原則不要と見込む。0.8.0 の API 差分 (上記 2 点) は本 SDK が利用していないため、まず sysroot を 0.8.0 で作り直してビルドが通ることを確認する
- sysroot は設定を変えない限り再利用されるため、追従は「`--rootfs-fetch-force` で sysroot を作り直して 0.8.0 でビルドできることを確認する」作業として扱う。バージョン固定の仕組みの追加はこの issue では行わない
- ビルドが通らない場合に限り、`third_party/libcamerac/` の実装と `CMakeLists.txt` の `camerac` ターゲットを 0.8.0 に合わせて修正する
- ラズパイ実機の libcamera を 0.8 に更新できない場合は、0.8.0 でビルドした成果物を実機で動かせない。この状態で閉じず、実機の更新を追う `pending` にするか、更新待ちの別 issue に分けるかを決めてから closed にする
- `doc/faq.md` の `libcamera.so` の説明は、0.8 以降の soname に合わせて `libcamera.so.0.8` に修正する。`libcamerac.so` は libcamera 本体 (`libcamera0.8`) が入っていれば動くため、`libcamera.so` の同梱を要求する記述が実態と合っているかも併せて確認する
- 追随した内容は `CHANGES.md` の `develop` に `[CHANGE]` または `[UPDATE]` で記載する (カテゴリの判定は `shiguredo-changelog` の規約に従う)

## 完了条件

- `python3 run.py build raspberry-pi-os_armv8 --rootfs-fetch-force` で sysroot が `libcamera-dev` 0.8.0+rpt20261104-1 と `libcamera0.8` で生成され、`camerac` と `sora` のビルドが成功すること
- 生成された `_install/raspberry-pi-os_armv8/<config>/rootfs/.webrtc-build-sysroot.json` の `deb_files` に `libcamera-dev_0.8.0+rpt20261104-1_arm64.deb` と `libcamera0.8_0.8.0+rpt20261104-1_arm64.deb` が含まれること
- `_install/raspberry-pi-os_armv8/<config>/sora/lib/libcamerac.so` について、`readelf -d` の `NEEDED` に `libcamera.so.0.8` と `libcamera-base.so.0.8` が含まれること
- サンプル (`python3 examples/sumomo/run.py build raspberry-pi-os_armv8 --rootfs-fetch-force`) もビルドできること。サンプル側の sysroot 設定 `examples/sysroot/raspberry-pi-os_armv8.json` は SDK 側と同一だが、sysroot は別ディレクトリ (`examples/_install/raspberry-pi-os_armv8/<config>/rootfs`) に生成されるため、`--rootfs-fetch-force` はサンプル側でも指定する
- ラズパイ実機の `libcamera0.8` が確認できていること。実機を 0.8 に更新できず、かつ実機での確認が取れない場合は、closed にせず `pending` にするか、実機更新を追う別 issue に分けてから closed にすること
- 実機を 0.8 に更新できた場合、`test_sumomo_raspberry_pi.py` の libcamera を使うケース (`use_libcamera=True`) で送受信が動作すること
- `doc/faq.md` の libcamera 関連の記述が 0.8 以降の soname と一致していること
- `CHANGES.md` に追従内容が記載されていること

## 解決方法

1. ラズパイ実機 (CI の self-hosted runner) の `libcamera0.x` のバージョンを確認する。0.7 系のままなら、実機を 0.8 に更新できるかを決める。更新できない場合は、手順 6 を実施できないため issue の扱いを決める
2. `python3 run.py build raspberry-pi-os_armv8 --rootfs-fetch-force` を実行し、sysroot の生成とビルドを確認する
3. `.webrtc-build-sysroot.json` の `deb_files` と、`readelf -d _install/raspberry-pi-os_armv8/release/sora/lib/libcamerac.so` の `NEEDED` を確認する (0.8 追従後は Debug ビルドなら `debug` に読み替える)
4. ビルドが失敗した場合は、エラー内容に応じて `third_party/libcamerac/libcamerac.cpp` と `third_party/libcamerac/libcamerac.h` (必要なら `CMakeLists.txt` の `camerac` ターゲット) を修正する
5. `python3 examples/sumomo/run.py build raspberry-pi-os_armv8 --rootfs-fetch-force` でサンプルも確認する
6. ラズパイ実機の E2E テスト (`e2e-test/test_sumomo_raspberry_pi.py`) を実行し、libcamera を利用した送受信が動くことを確認する
7. `doc/faq.md` の libcamera 関連の記述を 0.8 以降の soname に合わせて修正する
8. `CHANGES.md` の `develop` に追従内容を記載する

### 実施内容

- ソースコードの変更は不要だった。libcamera 0.8.0 のパブリック API 差分 (`orientationFromRotation()` / `transformFromRotation()` の `std::optional` 返却化、コントロール enum 名の接頭辞削除) はいずれも本 SDK が利用していない
- `libcamera-dev` が 0.8.0+rpt20261104-1 に上がったことで、`libcamerac.so` の `NEEDED` は `libcamera.so.0.8` と `libcamera-base.so.0.8` になった
- `CHANGES.md` に `[UPDATE]` で追従内容を記載した
- `doc/faq.md` と `skills/sora-cpp-sdk/SKILL.md` の libcamera の記述を実態に合わせて修正した。libcamera の共有ライブラリはシステムのライブラリディレクトリから読み込まれるため、実行ファイルと同じディレクトリに置いても読み込まれない。端末に libcamera のランタイムパッケージが要ることを明記した

### 確認結果

- CI の `Build sora-cpp-sdk for raspberry-pi-os_armv8` が成功した。生成された `examples/_build/raspberry-pi-os_armv8/release/sumomo/libcamerac.so` の `NEEDED` は `libcamera.so.0.8` と `libcamera-base.so.0.8`
- ラズパイ実機は `libcamera0.8` 0.8.0+rpt20261104-1 (`libcamera-dev` / `libcamera-ipa` / `libpisp1` を含む) がインストール済みで、`libcamera.so.0.8` は `ldconfig` のキャッシュから解決できる
- CI の `E2E Test for raspberry_pi` は `test_sumomo_raspberry_pi.py` の libcamera を使うケースを含めて成功した
- sysroot の生成は `buildbase.py` の `install_sysroot()` を Ubuntu 26.04 のコンテナで実行して確認し、`deb_files` に `libcamera-dev_0.8.0+rpt20261104-1_arm64.deb`、`libcamera0.8_0.8.0+rpt20261104-1_arm64.deb`、`libcamera-ipa`、`libpisp1` が含まれることを確認した
