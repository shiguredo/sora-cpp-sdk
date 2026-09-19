# SendDataChannel() が Sora の管理するラベルへ送信できてしまう問題を修正する

- Created: 2026-09-19
- Completed: YYYY-MM-DD
- Branch: feature/fix-restrict-send-datachannel-labels
- Polished: YYYY-MM-DD
- Reporter: @melpon

## 目的

`SoraSignaling::SendDataChannel()` は送信先ラベルを検証しないため、アプリケーションが Sora の管理するラベル (`signaling` / `stats` / `notify` / `push`) へ任意の文字列を送信できてしまう。Sora はこれらをプロトコル違反として扱い、接続を 4490 INTERNAL-ERROR で切断する。アプリケーションの単純なミスで接続が切れるのを防ぐため、送信先をユーザー定義ラベルと `rpc` に制限する。

## 現状

- `src/sora_signaling.cpp` の `SoraSignaling::SendDataChannel()` はラベルを検証せず `DataChannel::Send()` を呼ぶだけになっている
- `src/data_channel.cpp` の `DataChannel::Send()` はラベルの存在と開閉状態しか見ておらず、ラベルの種別を見ていない
- ラベルごとの仕様は次のとおり
  - `signaling`: シグナリングメッセージ専用。SDK 内部の `DoSendUpdate()` などが送る
  - `stats`: `{"type":"stats","reports":[...]}` 形式の統計応答専用。SDK 内部の `DoSendPong()` が送る
  - `notify` / `push`: Sora からクライアントへの受信専用 (`direction` は `recvonly`)
  - `rpc`: JSON-RPC 2.0 のリクエストをクライアントから送る。アプリケーションが送る正当な用途がある
  - `#` で始まるラベル: リアルタイムメッセージング用。アプリケーションが送る正当な用途がある
- 実測: `test/datachannel.cpp` は `OnDataChannel()` で開いたすべてのラベルにラベル文字列を送っていたため、`{"type":"switched"}` の受信直後に Sora から 4490 INTERNAL-ERROR で切断された。ユーザー定義ラベルにのみ送るよう変更すると 10 回の接続がすべて成功した
- `SoraSignaling::SendDataChannel()` は `DataChannel::Send()` の戻り値を捨てているため、送信できなかった場合も `true` を返す

## 設計方針

- `SoraSignaling::SendDataChannel()` でラベルを検証し、`signaling` / `stats` / `notify` / `push` および offer に含まれないラベルへの送信を拒否して `false` を返す
- 送信を許可するのは `#` で始まるラベルと `rpc`
- SDK 内部の送信 (`DoSendUpdate()` / `DoSendPong()`) も `SendDataChannel()` を経由しているため、内部送信用の private メソッドへ切り出してそちらを使う
- `SoraSignaling::SendDataChannel()` の戻り値に `DataChannel::Send()` の結果を反映する

## 完了条件

- `SendDataChannel("signaling" | "stats" | "notify" | "push", ...)` が `false` を返し、DataChannel へ送信されないこと
- `SendDataChannel("#label", ...)` と `SendDataChannel("rpc", ...)` は従来どおり送信できること
- 開いていないラベルへの送信が `false` を返すこと
- 回帰がないこと: `test/datachannel.cpp` と E2E テスト (`test_sumomo_data_channel_signaling`) が通ること
