# WebSocket の read 完了時に接続が破棄済みだと、破棄済みの Websocket を参照してクラッシュする

- Created: 2026-10-13
- Completed: {YYYY-MM-DD}
- Branch: feature/fix-signaling-read-after-clear-crash
- Polished: {YYYY-MM-DD}

## 目的

`sora-python-sdk` 経由のクライアントで、接続が破棄された後に WebSocket のデータを受信すると SIGSEGV でプロセスごと落ちるのを防ぐ。

3 ノードクラスターで 1 ノードを一時停止して復帰させる、というクラスターの障害試験で再現する。
クライアントのプロセスが落ちるため、アプリケーション側で回避できない。

## 現状

### 発生事象

`Fatal Python error: Segmentation fault` でクライアントのプロセスが終了コード 139 で落ちる。

`sora-python-sdk` の wheel に含まれる `sora_sdk_ext` のシンボルから、次が分かっている (demangle 済み、外側から内側へ)。

```text
boost::asio::detail::scheduler::run(boost::system::error_code&)
boost::asio::detail::reactive_socket_recv_op<...>
boost::beast::websocket::stream<...>
sora::Websocket::OnRead(...)
sora::SoraSignaling::DoRead()
sora::Websocket::Read(...)
boost::asio::post(boost::asio::strand<...>, ...)
```

落ちているのは Python のスレッドではなく `io_context` のスレッドで、実行中のハンドラは WebSocket の read 完了である。

### 再現手順

3 ノードのクラスターを用意する。

1. 配信者を 1 台目のノードに接続する
2. 視聴者を 2 台目と 3 台目のノードに接続し、両方に映像が届くことを確認する
3. 3 台目のノードのプロセスを、`net_ticktime` より長い時間 (例: 35 秒) SIGSTOP する
4. 3 台目のノードの視聴者の接続が ICE の失敗で切れるのを待つ
5. SIGCONT でノードを復帰させる

復帰の直後にクライアントが SIGSEGV する。2 回試して 2 回とも再現した。
`sora` リポジトリの `e2e-test/test_cluster_hung_node_recovery.py` でも同じ手順で 3 回連続して再現している。

### 原因

`src/sora_signaling.cpp` の `SoraSignaling::OnRead` は、エラーの無い read 完了で `state_ != State::Connected` の場合も `DoRead()` を呼ぶ。

一方 `SoraSignaling::Clear()` は `ws_ = nullptr` と `state_ = State::Closed` を行う。
`Clear()` は `SoraSignaling::SendOnDisconnect` が `io_context` に post するラムダから呼ばれるため、既にキューに入っている read 完了より先に実行されうる。

したがって、次の順序で `ws_` が null のまま `DoRead()` が走る。

1. 相手ノードの一時停止で ICE が失敗し、`SendOnDisconnect` から `Clear()` が呼ばれて `ws_ = nullptr`、`state_ = State::Closed` になる
2. 一時停止していたノードが復帰し、バッファしていたデータを送る
3. 保留されていた read が成功として完了し、`SoraSignaling::OnRead` が `state_ != State::Connected` の分岐で `DoRead()` を呼ぶ
4. `SoraSignaling::DoRead()` は null の `ws_` を `operator->` で参照する。null の `shared_ptr` の `operator->` はそのまま `this == nullptr` で `Websocket::Read` に入るため、`Websocket::Read` の `boost::asio::post(strand_, ...)` がメンバの `strand_` を読んだところで落ちる

`issues/closed/0058-bug-fix-websocket-cancel-null-crash.md` は、同じく `Clear()` の後に null の `ws_` を触る問題を、切断タイマーと DataChannel close のハンドラについて直している。read 完了の経路は残っている。

## 設計方針

- `state_ != State::Connected` のときに read を再開しない。`SoraSignaling::OnRead` の該当分岐では `DoRead()` を呼ばずに戻す
- `SoraSignaling::DoRead()` は `ws_` を参照する前に `ws_` と `state_` を確認し、進めない場合は戻す
- `Clear()` の後に read を再開しないことを、ハンドラの実行順序に依存せず保証する

## 完了条件

- 上記の再現手順でクライアントのプロセスが落ちないこと
- `sora` リポジトリの `e2e-test/test_cluster_hung_node_recovery.py` が成功すること
- 既存の E2E テストとユニットテストが通ること

## 関連 issue

- `issues/closed/0058-bug-fix-websocket-cancel-null-crash.md`: `Clear()` の後に null の `ws_` を参照する同じ原因の別経路
- `sora` リポジトリの `issues/0479-bug-fix-hung-node-recovery-session-destroy.md`: このクラッシュに先立って発生するセッション破棄の問題 (別リポジトリ)

## 解決方法

（実装時に記入）
