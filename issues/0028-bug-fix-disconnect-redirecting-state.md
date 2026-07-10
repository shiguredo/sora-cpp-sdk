# Disconnect が Redirecting 状態を処理せず DoInternalDisconnect の assert でクラッシュする

- Priority: High
- Created: 2026-07-10
- Completed: {YYYY-MM-DD}
- Model: DeepSeek V4 Pro
- Branch: feature/fix-disconnect-redirecting-state
- Polished: 2026-07-10
## 目的

`SoraSignaling::Disconnect()` が `Redirecting` 状態を処理しないため、`Redirecting` 中に切断すると `DoInternalDisconnect()` にフォールスルーする。debug ビルドでは冒頭の `assert(state_ == State::Connected)` でクラッシュし、release ビルドでは assert が無視されて `Closing` に遷移し、リダイレクト中の旧 WebSocket に対して切断処理が走って状態が不整合になる（`DoInternalDisconnect()` は `Connected` 状態を前提とする）。メカニズムの詳細は「現状」に記す。

## 優先度根拠

切断シーケンスでクラッシュまたは状態不整合が発生する。シグナリングのリダイレクトは Sora サーバの標準機能であり発生頻度が高い。High。

## 現状

`src/sora_signaling.cpp:176-196` の該当箇所を抜粋する。

```cpp
void SoraSignaling::Disconnect() {
  boost::asio::post(*config_.io_context, [self = shared_from_this()]() {
    if (self->state_ == State::Init) { /* Closed にして */ return; }
    if (self->state_ == State::Connecting) {
      self->SendOnDisconnect(SoraSignalingErrorCode::CLOSE_SUCCEEDED,
                             "Close was called in connecting");
      return;
    }
    if (self->state_ == State::Closing) { return; }
    if (self->state_ == State::Closed) { return; }
    // Redirecting がチェックされない → DoInternalDisconnect に到達
    self->DoInternalDisconnect(std::nullopt, "", "");
  });
}
```

`DoInternalDisconnect()`（`src/sora_signaling.cpp:649-804`）は冒頭 653 行に `assert(state_ == State::Connected)` を持ち、直後の 655 行で `state_ = State::Closing` に遷移する。

`Redirecting` 状態は `Redirect()`（`src/sora_signaling.cpp:220-299`）で設定され、`OnRedirect()`（`301-325`）で `Connected` に戻る。この区間には次の非同期待ちがあり、待ち中に `Disconnect()` の post が割り込むと `Redirecting` 状態のまま `DoInternalDisconnect()` に到達する:

- `ws_->Read()`（225 行）の完了待ち
- リダイレクト用の `connection_timeout_timer_.async_wait()`（247-257 行）
- 新しい WebSocket `new_ws->Connect()`（285 行）の完了待ち

なお `Redirect()` 内の各コールバック（229 / 234 / 304 行）は `state_ != State::Redirecting` で早期 return するため、`Disconnect()` 側で正しく状態遷移させればリダイレクトの残処理は自然に停止する。防御が欠けているのは `Disconnect()` 側のみである。

## 設計方針

`Disconnect()` に `State::Redirecting` の分岐を追加する。`Connecting` 分岐の直後（`Closing` 分岐の前）に、`Connecting` と同じ形で次を挿入する:

```cpp
if (self->state_ == State::Redirecting) {
  self->SendOnDisconnect(SoraSignalingErrorCode::CLOSE_SUCCEEDED,
                         "Close was called in redirecting");
  return;
}
```

- `SendOnDisconnect()`（`1415-1428`）は `Clear()`（`1474-1493`）を post し、`Clear()` が `connection_timeout_timer_` を cancel し `ws_` を解放して `state_ = State::Closed` にする。これによりリダイレクト用タイマと旧 WebSocket が確実に片付く。
- `SendOnDisconnect()` は `Clear()` を post で遅延実行するため、`Clear()` 実行前に Redirect の Read コールバック（225 行）が `Redirecting` のまま一度走り、新 WebSocket の接続開始まで進む場合がある。`Connecting` 分岐と同じ挙動で安全。
- `new_ws`（`Redirect()` 285 行で Connect 中）は `OnRedirect()` の `state_ != State::Redirecting` 早期 return でコールバックが放棄され、`Connect()` 完了時に `std::bind` が保持する `shared_ptr` が解放されて破棄される（`ws_` には未代入のため `Clear()` の対象外）。
- リダイレクト用 `connection_timeout_timer_` が `Disconnect()` の直前に発火していた場合、そのコールバック（255 行）も `SendOnDisconnect` を呼ぶため `OnDisconnect` が二重通知されうる。これは全経路を対象とする 0031（`SendOnDisconnect` の多重呼び出しガード）で包括的に防ぐ。本 issue は `Redirecting` 分岐の新設に限定する。
- `ws_->Cancel()` を明示的に呼ぶ方針は採らない。`Connecting` 分岐と非対称になり、旧 ws の Read コールバック（`Redirect()` 225 行）を `operation_aborted` で発火させてリダイレクトの残処理を誘発するため、`SendOnDisconnect` + `Clear()` に一本化する。

## 完了条件

- `Redirecting` 状態で `Disconnect()` が呼ばれても assert クラッシュや状態不整合が発生しないこと。ここでの「状態不整合」とは次を指す:
  - `OnDisconnect` が最大 1 回だけ呼ばれる。ただしリダイレクト用タイマが `Disconnect()` より先に発火する競合ケースは 0031 で対応し、0028 単体の対象外とする
  - `Clear()` 後に旧 `ws_` へアクセスしない
  - `new_ws` の接続が放棄後に残らない
- `src/sora_signaling.cpp:176-196` の `Disconnect()` に `State::Redirecting` の分岐が追加され、`SendOnDisconnect` + `Clear()` でリソースが解放されること
- 後方互換への影響はない（修正前は debug でクラッシュ / release で状態不整合であり、正常な切断になっていなかった）
- 検証方法: リダイレクト中に `Disconnect()` を呼ぶ手動確認、または `e2e-test/` へリダイレクト中切断のケースを追加して確認する（状態遷移だけを対象とする単体テストの仕組みは無い）
- `CHANGES.md` の `## develop` 配下（`### misc` より前の `src/` コア修正の `[FIX]` 群）に `[FIX]` エントリを追記する。`### misc` は Examples / CI / tooling 用のため使わない:
  ```
  - [FIX] Disconnect が Redirecting 状態を処理せず DoInternalDisconnect の assert でクラッシュする問題を修正する
    - @<担当者>
  ```
