# Clear() 後の WebSocket read 完了で null の ws_ を参照してクラッシュする

- Created: 2026-10-13
- Completed: {YYYY-MM-DD}
- Branch: feature/fix-signaling-read-after-clear-crash
- Polished: 2026-10-13

## 目的

`sora-python-sdk` 経由を含むクライアントで、接続の終了処理後に WebSocket の read が成功として完了しても、null の `ws_` を参照してプロセスが落ちないようにする。

## 現状

### 発生事象

現行コードでは、`Clear()` の後に保留中の read が成功として完了すると、次の経路で null の `ws_` に対して新しい read を開始する。

```text
Websocket::OnRead
  → SoraSignaling::DoRead が登録したコールバック
  → SoraSignaling::OnRead
  → SoraSignaling::DoRead
  → ws_->Read
```

これは実行ログから抽出したバックトレースではなく、`sora_signaling.cpp` と `websocket.cpp` から確認できる呼び出し経路である。null のポインタを使った非静的メンバ関数の呼び出しは未定義動作であり、SIGSEGV などのクラッシュにつながる。

### 再現手順

再現確認では、実 WebSocket 通信で次の順序を作り、ログ等でその順序を確認する。read 完了ハンドラが処理されるまで `io_context` とクライアントを稼働させる。モックやスタブは使わない。

1. WebSocket を保持する設定で接続し、`SoraSignaling::DoRead()` が開始した read を保留させる。DataChannel シグナリングを無効にするか、`ignore_disconnect_websocket = false` にする
2. read が保留中のまま WebRTC の通信経路を遮断し、`SoraSignaling::OnConnectionChange` に `PeerConnectionState::kFailed` が通知される状態にする
3. `SendOnDisconnect` が post したラムダで `Clear()` が実行され、`ws_ == nullptr`、`state_ == State::Closed` になったことを確認する
4. 保持されている WebSocket に相手からメッセージを送信し、手順 1 の read が成功として完了する状態にする

ノードの一時停止と復帰を利用する場合も、固定の停止時間ではなく、上記の `Clear()` 後に成功 read が完了する順序を確認する。障害試験でクライアントが落ちなかっただけでは、この経路を検証できたとはしない。

### 原因

`sora_signaling.cpp` の `SoraSignaling::OnRead` は、エラーの無い read 完了で `state_ != State::Connected` の場合も `DoRead()` を呼ぶ。

一方 `SoraSignaling::Clear()` は `ws_ = nullptr` と `state_ = State::Closed` を行う。
`Clear()` は `SoraSignaling::SendOnDisconnect` が `io_context` に post するラムダから呼ばれる。`Clear()` は保留中の WebSocket read をキャンセルせず、`DoRead()` のコールバックも `ws = ws_` を捕捉して旧 `Websocket` を保持するため、`Clear()` 後にもその read は完了しうる。参照する `Websocket` 自体が破棄済みなのではなく、新たな read の開始に使う現在の `ws_` が null になっている。

したがって、次の順序で `ws_` が null のまま `DoRead()` が走る。

1. read の保留中に `SendOnDisconnect` から `Clear()` が呼ばれて `ws_ = nullptr`、`state_ = State::Closed` になる
2. 旧 `Websocket` が保持されている間に相手からメッセージを受信する
3. 保留されていた read が成功として完了し、`SoraSignaling::OnRead` が `state_ != State::Connected` の分岐で `DoRead()` を呼ぶ
4. `SoraSignaling::DoRead()` は null の `ws_` を検査せずに `ws_->Read(...)` を呼ぶ。`websocket.cpp` の `Websocket::Read` はメンバの `strand_` を使って `boost::asio::post` を行うため、null のポインタからの呼び出しを安全に扱えない

過去 issue 「切断タイマーが null の ws_ に対して Cancel() を呼び SIGSEGV でクラッシュする」は、同じく `Clear()` の後に null の `ws_` を触る問題を、切断タイマーと DataChannel close のハンドラについて直している。read 完了の経路は残っている。

## 設計方針

- 変更対象は `sora_signaling.cpp` の `SoraSignaling::OnRead` と `SoraSignaling::DoRead` とする
- `SoraSignaling::OnRead` の成功分岐では、`state_ == State::Closed` または `ws_ == nullptr` なら read を再開せずに戻す。`Connected` 以外の状態でメッセージを処理しない既存の挙動は維持する
- `State::Closing` で read が成功した場合は、メッセージを処理せず `DoRead()` を継続し、次の close またはエラーを待つ。既存のエラー分岐から `on_ws_close_` を呼ぶフローを維持する
- `SoraSignaling::DoRead()` は `ws_` を参照する前に、`ws_ != nullptr` かつ `state_` が `Connected` または `Closing` であることを確認し、それ以外は戻す
- `Clear()` により `Closed` になった接続では、成功 read の完了順序にかかわらず read を再開しないよう、`OnRead` と `DoRead` の両方で防御する
- Python 経由の検証では、修正済み C++ SDK を組み込んだ `sora-python-sdk` の wheel をビルドし、その wheel を実際に使用する。既存の配布 wheel のままでは C++ SDK の修正を検証できない

## 完了条件

- `Clear()` 後に保留 read が成功として完了する順序を実 WebSocket 通信で確認し、null の `ws_` を参照せず、read を再開せずに戻り、クライアントのプロセスが落ちないこと
- クライアント起点の通常の `Disconnect()` により `Closing` になり、通常メッセージを受信してから WebSocket が正常に閉じる場合も、read が継続され、`OnDisconnect` が `CLOSE_SUCCEEDED` で 1 回通知されること。read を止めたことにより切断完了が失われないこと
- 既存の E2E テストとユニットテストが通ること

## 関連 issue

- 「切断タイマーが null の ws_ に対して Cancel() を呼び SIGSEGV でクラッシュする」: `Clear()` の後に null の `ws_` を参照する同じ原因の別経路

## 解決方法

（実装時に記入）
