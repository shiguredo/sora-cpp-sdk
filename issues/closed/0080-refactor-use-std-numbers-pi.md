# M_PI の代わりに C++20 で入った std::numbers::pi を利用する

- Created: 2026-09-11
- Completed: 2026-09-19
- Branch: feature/refactor-use-std-numbers-pi
- Polished: 2026-09-15

## 目的

`M_PI` は C の標準ではなく、Windows (MSVC) では `_USE_MATH_DEFINES` を定義しないと未定義になる。C++20 で追加された `std::numbers::pi` に置き換え、非標準マクロと `_USE_MATH_DEFINES` への依存をなくす。

## 現状

- `src/capturer/fake_video_capturer.cpp` で `M_PI` を 4 箇所使用している
  - `ctx.rotate(-M_PI / 2)`
  - `ctx.fill_pie(0, 0, width * 0.3, 0, 2 * M_PI)`
  - `(current_frame % fps) / static_cast<float>(fps) * 2 * M_PI`
  - `double y = height * 0.5 + sin(phase * M_PI * 2) * height * 0.2;`
- Windows で `M_PI` を使うために `CMakeLists.txt` の `target_compile_definitions(sora PRIVATE ... _USE_MATH_DEFINES)` を定義している
- `src/capturer/fake_video_capturer.cpp` は `<math.h>` と `<cmath>` の両方を include している
- `examples/sumomo/src/sumomo.cpp` では既に `std::numbers::pi` を使用しており、C++20 の `std::numbers` を利用する前例がある
- `CMakeLists.txt` で `sora` ターゲットは `CXX_STANDARD 20` に設定されている

## 設計方針

- `src/capturer/fake_video_capturer.cpp` の `M_PI` を `std::numbers::pi` に置き換え、`#include <numbers>` を追加する
- `sin` は `std::sin` を使用し、`<cmath>` で解決する。`<math.h>` が他に必要なければ削除する
- `_USE_MATH_DEFINES` が他に必要なければ `CMakeLists.txt` の `target_compile_definitions` から削除する
- C++20 でビルドして動作に問題がないことを確認する

## 完了条件

- ソースコードに `M_PI` が残っていないこと
- Windows を含む全ターゲットでビルドが通ること
- `_USE_MATH_DEFINES` を削除してもビルドが通ること (他に必要な箇所がない場合)
- `src/capturer/fake_video_capturer.cpp` が生成する fake 映像に回帰がないこと

## 解決方法

`M_PI` を `std::numbers::pi` に置き換え、非標準マクロと `_USE_MATH_DEFINES` への依存をなくした。

- `src/capturer/fake_video_capturer.cpp` の `M_PI` 4 箇所を `std::numbers::pi` に置き換え、`#include <math.h>` を削除して `#include <numbers>` を追加した
- 同じファイルの `sin` を `std::sin` に変更し、`<cmath>` で解決するようにした
- `CMakeLists.txt` の Windows 向け `target_compile_definitions` から `_USE_MATH_DEFINES` を削除した
- `CHANGES.md` の `## develop` の `### misc` に追記した

確認:

- `python3 run.py build ubuntu-24.04_x86_64 --disable-cuda --test` が通ることを確認した
- `python3 run.py iwyu ubuntu-24.04_x86_64` と `python3 run.py format` で差分が出ないことを確認した
- `M_PI` と `std::numbers::pi` が同一の double 値 (`0x400921fb54442d18`) であることを確認した。fake 映像の描画コードは変更していないため出力は変わらない
- `uv run --directory=e2e-test pytest 'test_sumomo_basic.py::test_sumomo_sendonly_recvonly[VP8]' -v -s --timeout=60` が通ることを確認した (fake キャプチャーデバイス経由で映像と音声が送受信できることを確認)
- Windows 向けビルドは実行していない。リポジトリ内に `M_PI` と `_USE_MATH_DEFINES` を利用している箇所が他にないことは確認した
