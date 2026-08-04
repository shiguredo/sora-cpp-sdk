# BaseRenderer::Sink の破棄と OnFrame 実行の競合を修正する

- Created: 2026-08-04
- Completed: {YYYY-MM-DD}
- Branch: feature/fix-sink-lifetime-race-with-onframe
- Polished: {YYYY-MM-DD}
- Reporter: @voluntas

## 目的

`BaseRenderer::RemoveTrack()` が `Sink` を破棄するとき、フレーム配信スレッドが `Sink::OnFrame()` を実行中だと、解放済みの `Sink` オブジェクトにアクセスする use-after-free が原理上起こりうる。この競合を解消して、`RemoveTrack()` 実行後に in-flight の `OnFrame()` が存在しないことを保証する。

## 現状

`BaseRenderer::Sink` のデストラクタ (src/renderer/base_renderer.cpp) は `track_->RemoveSink(this)` を呼ぶ。

`BaseRenderer::RemoveTrack()` は `sinks_lock_` 保持中に `sinks_.erase()` で `Sink` を破棄するため、`~Sink` と `RemoveSink` も `sinks_lock_` 保持中に実行される。一方 `Sink::OnFrame()` は WebRTC のフレーム配信スレッドから呼ばれ、`frame_params_lock_` を取得して `Sink` のメンバにアクセスする。

WebRTC の `VideoSourceBaseGuarded` (VideoTrack の基底) は「sinks に対する操作はオブジェクト構築スレッドで行われる前提」の設計であり、`RemoveSink` の返却時に `OnFrame` が実行中でない保証はない。`RemoveSink` 実行中にデリバリスレッドが `OnFrame` を実行中なら、Sink ごと解放されたオブジェクトへのアクセスが起こりうる。

なお `OnFrame()` は `frame_params_lock_` も `Sink` のメンバとして保持するため、Sink のデストラクタがロックを取る設計ではこの問題は解決できない。

## 設計方針

- `RemoveTrack()` が `Sink` を破棄する前に、その `Sink` に対する in-flight の `OnFrame()` が完了していることを保証する仕組みを導入する
- 候補: `Sink` に `OnFrame()` 実行中を表すフラグを持たせ、`RemoveTrack()` 側で破棄前に完了を待ち合わせる。または WebRTC 側のスレッド契約を確認し、既存の暗黙の直列化を明示的な待ち合わせに置き換える
- 既存のロック順序 (`sinks_lock_` → `frame_params_lock_`) を維持する

## 完了条件

- `RemoveTrack()` 実行中に、フレーム配信スレッドが破棄済み `Sink` の `OnFrame()` を実行しないことがコードで保証されること (コードレビューで確認できること)
- 既存のロック順序 (`sinks_lock_` → `frame_params_lock_`) が維持されていること
- ローカルビルド (`python3 run.py build --test --disable-cuda macos_arm64`) と既存テストが通ること
- `python3 run.py format` で clang-format に差分が出ないこと

## 解決方法

未対応 (open)
