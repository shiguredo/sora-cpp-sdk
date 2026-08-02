# sdl_sample の SDL レンダラーを BaseRenderer に統合する

- Created: 2026-08-02
- Completed: {YYYY-MM-DD}
- Branch: feature/refactor-unify-sdl-sample-renderer
- Polished: {YYYY-MM-DD}
- Reporter: @voluntas

## 目的

`sdl_sample` の SDL レンダラーは `BaseRenderer` を継承しない独立実装で、枠割りロジックを独自に持っている。sumomo は `BaseRenderer` を継承しており、両者の挙動が乖離する。`sdl_sample` を `BaseRenderer` ベースに統合して、枠割り・描画のロジックを一元化する。

## 現状

- `examples/sdl_sample/src/sdl_renderer.h` の `SDLRenderer` は `class SDLRenderer {` として宣言されており、`sora::BaseRenderer` を継承していない
- `examples/sdl_sample/src/sdl_renderer.cpp` の `SDLRenderer::SetOutlines()` は独自実装で、ウィンドウアスペクトと映像アスペクトを考慮しない旧方式の枠割りになっている
- `BaseRenderer` の枠割り修正 (実測アスペクト採用・共通縮小・中央寄せ) が `sdl_sample` に反映されず、sumomo と `sdl_sample` でマルチ映像表示の挙動が異なる

## 設計方針

- `sdl_sample` の `SDLRenderer` を `sora::BaseRenderer` のサブクラスに変更し、`Render()` / `RenderThreadStarted()` / `RenderThreadFinished()` を実装する形に置き換える
- 独自の `SetOutlines()` と重複する枠割り・合成ロジックを削除して `BaseRenderer` 側に寄せる
- `sdl_sample` の機能 (SDL ウィンドウ・テクスチャ描画) は維持する

## 完了条件

- `sdl_sample` が `BaseRenderer` を継承してビルドが通ること (`python3 examples/sdl_sample/run.py build <target>`)
- 独自の `SetOutlines()` が削除されていること
- `BaseRenderer` の枠割り修正の挙動が `sdl_sample` にも適用されること
