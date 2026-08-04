# AddTrack の Sink 登録を sinks_lock_ 保護下に移す

- Created: 2026-08-04
- Completed: {YYYY-MM-DD}
- Branch: feature/refactor-move-addtrack-sink-registration-under-lock
- Polished: {YYYY-MM-DD}
- Reporter: @voluntas

## 目的

`BaseRenderer::AddTrack()` は `Sink` の生成と `AddOrUpdateSink` によるトラックへの登録を `sinks_lock_` 取得前に実行しており、WebRTC 内部の暗黙の直列化に依存している。登録操作を `sinks_lock_` 保護下に移して、暗黙依存を明示的なロック保護に変える。

## 現状

`BaseRenderer::AddTrack()` (src/renderer/base_renderer.cpp) は次の順で実行する。

1. `std::unique_ptr<Sink> sink(new Sink(this, track))` で Sink を生成する。`Sink` コンストラクタは `track_->AddOrUpdateSink(this, webrtc::VideoSinkWants())` を呼ぶ
2. `webrtc::MutexLock lock(&sinks_lock_)` を取得する
3. `sinks_` に追加して `SetOutlines()` を呼ぶ

`AddOrUpdateSink` は `sinks_lock_` 取得前に実行されるため、登録直後からフレーム配信スレッドが `Sink::OnFrame()` を並行して呼びうる。現状は Sink コンストラクタの初期化リストが先に完了し、枠未確定時 (`outline_width_ == 0`) の `OnFrame()` は `frame_params_lock_` 保護下の early return で抜けるため実害はないが、暗黙の直列化に依存した状態である。

## 設計方針

- `Sink` の生成と `AddOrUpdateSink` の実行を `sinks_lock_` 保護下に移す
- 既存のロック順序 (`sinks_lock_` → `frame_params_lock_`) を維持する。`Sink::OnFrame()` は `frame_params_lock_` のみを取得するため、`sinks_lock_` 保持中に Sink を生成してもデッドロックは発生しない (コードレビューで確認する)

## 完了条件

- `AddOrUpdateSink` が `sinks_lock_` 保護下で実行されること (コードレビューで確認できること)
- 既存のロック順序 (`sinks_lock_` → `frame_params_lock_`) が維持されていること
- ローカルビルド (`python3 run.py build --test --disable-cuda macos_arm64`) と既存テストが通ること
- `python3 run.py format` で clang-format に差分が出ないこと

## 解決方法

未対応 (open)
