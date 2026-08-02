# BaseRenderer の枠情報のデータレースを修正する

- Created: 2026-08-02
- Completed: {YYYY-MM-DD}
- Branch: feature/fix-base-renderer-outline-data-races
- Polished: {YYYY-MM-DD}
- Reporter: @voluntas

## 目的

`BaseRenderer::Sink` の枠情報 (outline) の一部が `frame_params_lock_` 保護外で読み書きされており、`Sink::OnFrame()` (ビデオスレッド) と `Sink::SetOutlineRect()` / `BaseRenderer::RenderThread()` (描画スレッド) の間でデータレースが発生する。ロック保護の範囲を正して、未定義動作を排除する。

## 現状

次の 2 箇所に `frame_params_lock_` 保護外の読み書きがある。

- `Sink::OnFrame()` 冒頭の早期リターン条件 (src/renderer/base_renderer.cpp): `outline_width_ == 0 || outline_height_ == 0` をロック取得前に読んでいる
- `Sink::SetOutlineRect()` 冒頭 (src/renderer/base_renderer.cpp): `outline_offset_x_` / `outline_offset_y_` を `frame_params_lock_` 取得前に書き換えている (幅・高さが同一の early return 経路ではロックを一切取らずに書き換える)

`BaseRenderer::RenderThread()` の合成ループは `sinks_lock_ → frame_params_lock_` の順でロックを取得して `GetOffsetX()` / `GetOffsetY()` を読むため、上記の保護外書き込みと競合しうる。

## 設計方針

- ロック取得を早期リターンの前に移動し、`outline_width_` / `outline_height_` / `outline_offset_x_` / `outline_offset_y_` の読み書きをすべて `frame_params_lock_` 保護下に収める
- 既存のロック順序 (`sinks_lock_ → frame_params_lock_`) とデッドロック回避の設計 (非再帰ミューテックスの自己ロック防止) を維持する
- `SetOutlineRect()` の early return (幅・高さが同一なら offset のみ更新) の挙動は変えない

## 完了条件

- 上記 2 箇所の枠情報の読み書きが `frame_params_lock_` 保護下になること
- ローカルビルド (`python3 run.py build --test --disable-cuda macos_arm64`) と既存テストが通ること
- `python3 run.py format` で clang-format に差分が出ないこと
