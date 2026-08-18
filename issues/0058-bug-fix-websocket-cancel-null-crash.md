# 切断タイマーが null の ws_ に対して Cancel() を呼び SIGSEGV でクラッシュする

- Created: 2026-08-18
- Completed: {YYYY-MM-DD}
- Branch: feature/fix-websocket-cancel-null-crash
- Polished: {YYYY-MM-DD}
- Reporter: @voluntas

## 目的

sora-python-sdk 経由の E2E テスト (DataChannel シグナリング有効 + パケロス環境で複数接続の確立・切断が重なる状況) で、I/O スレッドが `sora::Websocket::Cancel` の先頭で SIGSEGV になるクラッシュが発生した。

原因は `SoraSignaling` の切断タイムアウト処理が `ws_` を無検査で `Cancel()` する経路と、`SendOnDisconnect` → `Clear()` による `ws_ = nullptr` の競合。null の `shared_ptr` に対する `ws_->Cancel()` は `operator->` では落ちず、`this == nullptr` のまま `Websocket::Cancel` に入って最初のメンバアクセス (インライン化された `IsSSL()` の `https_proxy_` 読み出し) で null 近傍アクセスになる。クラッシュフレームが `Websocket::Cancel` の先頭に出る実際のクラッシュと整合する。

## 現状

ソースコードで確認済みの欠陥は 2 つある。

### 1. DoInternalDisconnect のタイマー / DC close ハンドラが ws_ を無検査で Cancel() する

`src/sora_signaling.cpp` の `SoraSignaling::DoInternalDisconnect` に、`self->ws_->Cancel()` を null チェックなしで呼ぶハンドラが 3 箇所ある。

- `using_datachannel_ && ws_connected_` パス: DC close 成功後に張る `closing_timeout_timer_` のハンドラ
- 同パス: `dc_->Close` のエラー (DC 切断タイムアウト) ハンドラ
- `!using_datachannel_ && ws_connected_` パス: `closing_timeout_timer_` のハンドラ

いずれも `self` (`shared_from_this()` した `SoraSignaling`) だけをキャプチャし、`Websocket` の `shared_ptr` を持たない。

一方 `SoraSignaling::SendOnDisconnect` は `io_context` に post したラムダの中で `Clear()` を呼び、`Clear()` は `closing_timeout_timer_.cancel()` と `ws_ = nullptr` を行う。Asio の `steady_timer` は満了して成功 `error_code` の完了ハンドラがキューに積まれた後は `cancel()` で取り消せないため、post の順序次第で次のシーケンスが成立する。単一 I/O スレッドでも起きる。

1. `closing_timeout_timer_` が満了し、成功 `error_code` の完了ハンドラがキューに積まれる
2. 別経路 (PeerConnection / DataChannel の状態変化など) から post されていた `SendOnDisconnect` のラムダが先に実行され、`Clear()` で `ws_ = nullptr` になる (タイマーの `cancel()` はもう効かない)
3. 残っていたタイマーハンドラが実行され、`self->ws_->Cancel()` が null の `ws_` に対して呼ばれて SIGSEGV

パケロスで WebSocket の close 完了が遅れるほどタイムアウト満了と切断完了通知が重なりやすく、競合の窓が広がる。

### 2. Websocket::Cancel 自体に null ガードがない

`src/websocket.cpp` の `Websocket::Cancel` は `IsSSL()` の結果だけで `wss_` / `ws_` を参照する。`IsSSL()` は `https_proxy_ || wss_ != nullptr` であり、HTTPS プロキシ構成では CONNECT 完了後の `OnReadProxy` で初めて `wss_` が生成されるため、それ以前に `Cancel()` が呼ばれると `IsSSL()` が true のまま null の `wss_` を参照して落ちる。

なお `Websocket` 内部の非同期ハンドラが raw `this` を使っている点 (`enable_shared_from_this` でない) は設計として脆いが、現行の `Websocket::Close` の呼び出し側 (`SoraSignaling::Redirect` と `SoraSignaling::OnConnect`) はいずれも `on_close` が `Websocket` の `shared_ptr` をキャプチャしており、pending 中に破棄される実害経路は存在しないため本 issue の範囲外とする。

## 設計方針

null になり得る `self->ws_` をハンドラから参照するのをやめ、ハンドラ生成時点の `Websocket` を `shared_ptr` でキャプチャして、それに対して `Cancel()` を呼ぶ。これで null 参照は構造的に消え、`Clear()` 後にハンドラが実行されても生きたオブジェクトへの `Cancel()` (無害な no-op 相当) になる。

あわせて `Websocket::Cancel` 側にも防御として null ガードを入れ、`wss_` / `ws_` が null なら何もしないようにする。

## 完了条件

- `Clear()` で `ws_ = nullptr` になった後に `closing_timeout_timer_` / DC close のハンドラが実行されても SIGSEGV が発生しない
- HTTPS プロキシ構成で `wss_` 生成前に `Cancel()` が呼ばれても落ちない
- 既存のビルドが通り、既存の E2E テストが通る

## 解決方法

- `src/sora_signaling.cpp` の `SoraSignaling::DoInternalDisconnect`: 上記 3 箇所のハンドラで `ws = ws_` を明示的にキャプチャし、`self->ws_->Cancel()` の代わりに `ws->Cancel()` を呼ぶ
- `src/websocket.cpp` の `Websocket::Cancel`: `IsSSL()` 分岐の前に `wss_` / `ws_` の null チェックを追加し、null なら何もしない

タイミング依存の競合のため決定的な再現テストは書けない (モック禁止)。回帰確認は既存の E2E テストで行う。
