# BaseRenderer の枠割りが回転映像のアスペクトを考慮しない

- Created: 2026-08-02
- Completed: {YYYY-MM-DD}
- Branch: feature/fix-base-renderer-rotation-aspect
- Polished: {YYYY-MM-DD}
- Reporter: @voluntas

## 目的

`BaseRenderer` の枠割りは入力映像のアスペクトを回転前の寸法から算出するため、90° / 270° 回転した映像では表示アスペクトと枠アスペクトが一致せず、枠内 letterbox が余分に広がる。回転後のアスペクトを考慮した枠割りに修正する。

## 現状

- `Sink::OnFrame()` (src/renderer/base_renderer.cpp) は scaled 経路で `webrtc::I420Buffer::Rotate()` を適用した後、回転後のバッファを表示する
- `BaseRenderer::SetOutlines()` の `frame_aspect` は `Sink::input_width_` / `Sink::input_height_` (回転前のフレームサイズ) から算出される
- そのため 90° / 270° 回転映像では、実表示アスペクトと枠アスペクトが乖離し、方針 A (代表 Sink の実測アスペクト採用) の「代表 Sink が枠にぴったり収まる」前提が崩れる

## 設計方針

- 代表 Sink のアスペクト算出時に回転を考慮する (回転後の寸法、または `frame.rotation()` に応じた寸法の入れ替えを使う)
- `Sink::OnFrame()` の per-cell letterbox 計算 (枠内センタリング) も回転後のアスペクトと整合させる
- 既存の `Sink::OnFrame()` の分岐構造を維持しつつ、変更範囲を最小化する

## 完了条件

- 90° / 270° 回転映像で枠割りと表示アスペクトが一致すること
- 回転なし映像の挙動が変わらないこと
- ローカルビルド (`python3 run.py build --test --disable-cuda macos_arm64`) と既存テストが通ること
