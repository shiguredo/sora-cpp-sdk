# Disconnect が Redirecting 状態を処理せず DoInternalDisconnect の assert クラッシュ

- Priority: High
- Created: 2026-07-10
- Polished: 2026-07-10

## 目的

`SoraSignaling::Disconnect()` は `Init`/`Connecting`/`Closing`/`Closed` 状態をチェックするが `Redirecting` 状態をチェックしていない。`Redirecting` 中に `Disconnect()` が呼ばれると `DoInternalDisconnect()` に到達し、`assert(state_ == State::Connected)` が debug ビルドでクラッシュする。release ビルドでは `Redirecting` 状態のまま不正な disconnect 処理が走る。

## 優先度根拠

切断シーケンスでクラッシュまたは状態矛盾が発生する。シグナリングのリダイレクトは Sora サーバの標準機能であり発生頻度が高い。High。

## 現状

`src/sora_signaling.cpp:176-195`:

```cpp
void SoraSignaling::Disconnect() {
  if (state_ == State::Init) { ... return; }
  if (state_ == State::Connecting) { ... return; }
  if (state_ == State::Closing || state_ == State::Closed) { return; }
  // Redirecting がチェックされない → DoInternalDisconnect に到達
  DoInternalDisconnect(...);
}
```

`DoInternalDisconnect` (649-653 行目) は `assert(state_ == State::Connected)` を持つ。

## 設計方針

`Disconnect()` に `State::Redirecting` のチェックを追加し、`Redirecting` 中は `ws_->Cancel()` 等で安全に切断する。

## 完了条件

- `Redirecting` 状態で `Disconnect()` が呼ばれても assert クラッシュや状態矛盾が発生しないこと
- `src/sora_signaling.cpp:176-195` の `Disconnect()` に `State::Redirecting` のチェックが追加されていること
- `CHANGES.md` の `## develop` 配下、`### misc` セクションに `[FIX]` エントリを追記する:
  ```
  - [FIX] Disconnect が Redirecting 状態を処理せず DoInternalDisconnect の assert クラッシュするのを修正する
    - @<担当者>
  ```
