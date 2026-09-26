# SendDataChannel() の送信先をユーザー定義ラベルに制限し、rpc へ送る SendRpc() を追加する

- Created: 2026-09-19
- Completed: 2026-09-26
- Branch: feature/fix-restrict-send-datachannel-labels
- Polished: 2026-09-19
- Reporter: @melpon

## 目的

`SoraSignaling::SendDataChannel()` は送信先ラベルを検証しないため、アプリケーションが Sora の管理するラベル (`signaling` / `stats` / `notify` / `push` / `rpc`) へ任意の文字列を送信できてしまう。`signaling` / `stats` / `notify` / `push` では Sora がこれをプロトコル違反として扱い、接続を 4490 INTERNAL-ERROR で切断する。`rpc` は JSON-RPC 2.0 のメッセージ専用であり、アプリケーションが任意の文字列を送ると JSON-RPC として不正なメッセージになる。

送信先をユーザー定義ラベル (`#` で始まるラベル) に制限する。`rpc` への送信を塞ぐだけではアプリケーションが Sora の RPC 機能を利用する手段が無くなるため、SDK が JSON-RPC 2.0 のリクエストを組み立てて送信する `SendRpc()` も同時に追加する。

## 現状

- `src/sora_signaling.cpp` の `SoraSignaling::SendDataChannel()` はラベルを検証せず `DataChannel::Send()` を呼ぶだけになっている
- `src/data_channel.cpp` の `DataChannel::Send()` はラベルの存在と開閉状態しか見ておらず、ラベルの種別を見ていない
- ラベルごとの仕様は次のとおり
  - `signaling`: シグナリングメッセージ専用。SDK 内部の `DoSendUpdate()` などが送る
  - `stats`: `{"type":"stats","reports":[...]}` 形式の統計応答専用。SDK 内部の `DoSendPong()` が送る
  - `notify` / `push`: Sora からクライアントへの受信専用 (`direction` は `recvonly`)
  - `rpc`: Sora の RPC 機能 (JSON-RPC 2.0 over DataChannel) 専用。アプリケーションが Sora の API を呼び出すために使うラベルであり、Sora が管理する。Rust SDK では `SoraConnectionHandle::send_rpc_request` が JSON-RPC 2.0 メッセージを組み立てて送信し、汎用の送信 API (`send_message`) は内部ラベル (`signaling` / `stats` / `push` / `notify` / `rpc`) を一律に拒否する。C++ SDK には RPC リクエストを送る専用 API が無い
  - `#` で始まるラベル: リアルタイムメッセージング用。アプリケーションが送る正当な用途がある
- `SoraSignalingObserver::OnRpc()` は rpc ラベルで受信したメッセージの生データを通知するだけで、送信する API が無い
- 実測: `test/datachannel.cpp` は `OnDataChannel()` で開いたすべてのラベルにラベル文字列を送っていたため、`{"type":"switched"}` の受信直後に Sora から 4490 INTERNAL-ERROR で切断された。ユーザー定義ラベルにのみ送るよう変更すると 10 回の接続がすべて成功した
- `SoraSignaling::SendDataChannel()` は `DataChannel::Send()` の戻り値を捨てているため、送信できなかった場合も `true` を返す
- Sora の RPC 機能は、`sora.conf` で `data_channel_rpc` が `true` になっていて、DataChannel 経由のシグナリングを利用し、認証成功時に `rpc_methods` が払い出されている場合に利用できる
- `rpc` ラベルが使えるかどうかは offer の `data_channels` で分かる。SDK は offer の `data_channels` を `dc_labels_` に保持しているため、`DataChannel::IsOpen("rpc")` で送信可能か判定できる
- RPC の `method` は `{RPC 経由での HTTP API の呼出が導入された Sora のバージョン}/{HTTP API 名}` 形式、`params` は HTTP API に渡す JSON を指定する。`id` を付けない場合は Notification になり Sora はレスポンスを返さない

## 設計方針

### `SendDataChannel()` のラベル検証

- `SoraSignaling::SendDataChannel()` でラベルを検証し、`signaling` / `stats` / `notify` / `push` / `rpc` および offer に含まれないラベルへの送信を拒否して `false` を返す
- 送信を許可するのは `#` で始まるラベルだけにする。Sora が管理するラベルを個別に許可する例外を作らず、Sora のプロトコルを使う送信は SDK 内部の送信経路だけに閉じ込める
- SDK 内部の送信 (`DoSendUpdate()` / `DoSendPong()`) も `SendDataChannel()` を経由しているため、内部送信用の private メソッドへ切り出してそちらを使う。`SendRpc()` もこの経路を使う
- `SoraSignaling::SendDataChannel()` の戻り値に `DataChannel::Send()` の結果を反映する
- `test/datachannel.cpp` の `OnDataChannel()` は、SDK が開いたことを通知するすべてのラベル (`signaling` / `stats` / `notify` / `push` / `rpc` / `#` で始まるラベル) にラベル文字列を送っている。修正後は管理ラベルへの送信が `false` を返してテストが `std::exit(1)` するため、ユーザー定義の `#` で始まるラベルにのみ送信するよう変更する

### `SendRpc()` の追加

`SoraSignaling` に RPC リクエストを送信するメソッドを追加する。

```cpp
// JSON-RPC 2.0 リクエストを rpc ラベルで送信する。
// id はアプリケーションが指定し、SoraSignalingObserver::OnRpc() に届くレスポンスの id と突き合わせる。
// id が std::nullopt の場合は id を含めない (JSON-RPC 2.0 の Notification になり Sora はレスポンスを返さない)。
// params は JSON-RPC 2.0 の params にそのまま使い、std::nullopt の場合は params を含めない。
// 送信できた場合は true を返す。
bool SendRpc(std::optional<uint64_t> id,
             const std::string& method,
             const std::optional<boost::json::value>& params);
```

- SDK が `{"jsonrpc":"2.0","id":<id>,"method":<method>,"params":<params>}` を組み立てる
- `id` はアプリケーションが指定する。レスポンスの判別はアプリケーションが `OnRpc()` に届く JSON の `id` で行うため、SDK は id を採番しない
- `id` が `std::nullopt` の場合は `id` メンバーを含めない
- `params` が `std::nullopt` の場合は `params` メンバーを含めない。JSON-RPC 2.0 では `params` は省略可能であり、含める場合は Structured value (Array か Object) でなければならない。このため `"params": null` は送らない (Sora の RPC の型定義でも `params?` と省略可能になっている)
- `params` に Object でも Array でもない値を指定した場合は、JSON-RPC 2.0 の要件を満たさないため送信せずに `false` を返す
- 送信は SDK 内部の送信経路を使う。`SendDataChannel()` はアプリケーション向けの入口であり、`rpc` へは送れない
- `rpc` ラベルが開いていない場合は送信せずに `false` を返す (RPC が無効な Sora に接続した場合や、接続が確立していない場合)
- レスポンスの相関、タイムアウト、JSON-RPC 2.0 の検証は SDK では行わない。レスポンスの解釈はアプリケーションが `OnRpc()` で行う
- `SoraSignalingObserver::OnRpc()` は変更しない
- `method` が `rpc_methods` に含まれているかの検証は SDK では行わない。利用できないメソッドは Sora が error response (-32601) を返す

## 完了条件

- `SendDataChannel("signaling" | "stats" | "notify" | "push" | "rpc", ...)` が `false` を返し、DataChannel へ送信されないこと
- `SendDataChannel("#label", ...)` は、対応するラベルが offer に含まれていて開いている場合従来どおり送信できること
- `#` で始まるラベル以外のラベル、offer に含まれないラベル、および開いていないラベルへの送信が `false` を返すこと
- SDK 内部の送信 (`DoSendUpdate()` / `DoSendPong()` / `SendRpc()`) がラベル検証の影響を受けずに送信できること
- `SendRpc()` が `{"jsonrpc":"2.0","id":...,"method":...,"params":...}` を rpc ラベルで送信し、指定した `id` がそのまま含まれること
- `id` に `std::nullopt` を指定した場合に `id` を含めないこと
- `params` に `std::nullopt` を指定した場合に `params` を含めないこと
- `params` に Object でも Array でもない値を指定した場合に送信せずに `false` を返すこと
- `rpc` ラベルが開いていない場合に `SendRpc()` が `false` を返し、送信しないこと
- RPC が有効な Sora に接続した場合、`SendRpc()` で送信したリクエストのレスポンスが `OnRpc()` で通知されること
- 既存の `SoraSignalingObserver::OnRpc()` の動作が変わらないこと
- `CHANGES.md` の `## develop` に `SendDataChannel()` の送信先の制限 (`[CHANGE]`) と `SendRpc()` の追加 (`[ADD]`) を追記すること
- ビルドが通り、既存のテストに回帰がないこと

## テスト方針

- `#` で始まるラベルにのみ送信するよう変更した `test/datachannel.cpp` と E2E テスト (`test_sumomo_data_channel_signaling`) で回帰がないことを確認する
- RPC の実際の送受信は Sora サーバの `data_channel_rpc` が有効で `rpc_methods` が払い出されている場合にしか確認できない。`e2e-test/` の Sora で RPC を有効にできる場合は、`SendRpc()` のレスポンスが `OnRpc()` に届くことを確認する
- RPC を有効にできない場合は、`SendRpc()` が `rpc` ラベル未オープンで `false` を返すことだけを確認する
- モックやスタブは利用しない

## 変更対象

- `include/sora/sora_signaling.h` (`SendDataChannel()` の戻り値の反映、`SendRpc()` の追加)
- `src/sora_signaling.cpp` (ラベル検証、内部送信経路の切り出し、JSON-RPC 2.0 の組み立て、rpc ラベルへの送信)
- `test/datachannel.cpp` (`#` で始まるラベルにのみ送信するよう変更)
- `skills/sora-cpp-sdk/SKILL.md` (送信できるラベルの説明、RPC の使い方)
- `CHANGES.md`

## 解決方法

- `SoraSignaling::SendDataChannel()` でラベルを検証するようにした
  - 送信できるのは `#` で始まるユーザー定義ラベルだけとし、Sora が管理するラベル (`signaling` / `stats` / `notify` / `push` / `rpc`) と offer の `data_channels` に含まれないラベルへの送信は `false` を返す
  - `DataChannel::Send()` の結果を戻り値に反映するようにした
- SDK 内部の送信経路 `DoSendDataChannel()` を追加し、`DoSendUpdate()` と `DoSendPong()` をこの経路に切り替えた
  - ラベル検証はアプリケーション向けの `SendDataChannel()` にだけ適用され、SDK 内部の送信は影響を受けない
- JSON-RPC 2.0 のリクエストを `rpc` ラベルで送信する `SendRpc()` を追加した
  - `{"jsonrpc":"2.0","id":<id>,"method":<method>,"params":<params>}` を組み立てる。`id` と `params` は省略でき、`params` に Object でも Array でもない値を指定した場合は送信せず `false` を返す
  - `rpc` ラベルが開いていない場合も送信せず `false` を返す
- `test/datachannel.cpp` に次の検証を追加した
  - Sora が管理するラベルへの `SendDataChannel()` が `false` を返すこと
  - offer に含まれないラベルへの `SendDataChannel()` が `false` を返すこと
  - 接続前の `SendRpc()` と、`params` が Object でも Array でもない `SendRpc()` が `false` を返すこと
  - `SendRpc()` のレスポンスが `OnRpc()` に届くこと
- `test/datachannel_closed.cpp` を削除した
  - このテストはアプリケーションから signaling DataChannel へ `{"type":"disconnect"}` を送ることで Sora に DataChannel を閉じさせていたが、本対応でこの送信ができなくなった
  - 代替として `PeerConnection::Close()` を試したが、libwebrtc はシャットダウン中に `DataChannelObserver` への通知を行わないため検知できず、Sora 側にも DataChannel だけを閉じる公開 API が無いため、この検知経路はアプリケーションから再現できない
  - クライアント起点の `Disconnect()` は `test/connect_disconnect.cpp` と `test/datachannel.cpp` で確認済みのため、置き換えのテストは追加していない
  - `test/CMakeLists.txt` と `run.py` の `TEST_DATACHANNEL_CLOSED` も削除した
- `skills/sora-cpp-sdk/SKILL.md` に送信できるラベルの制限と Sora の RPC の使い方を追記した
- `CHANGES.md` の `## develop` に追記した
  - `SendDataChannel()` の送信先の制限はバグ修正のため `[FIX]`、`SendRpc()` の追加は `[ADD]` として記載した

### 確認したこと

- `python3 run.py build ubuntu-24.04_x86_64 --test --disable-cuda` が通ること
- `test/datachannel` を実 Sora に対して実行し、10 回の接続がすべて成功すること
- `SendRpc()` が `{"jsonrpc":"2.0","id":1,"method":"...","params":{...}}` を送信すること
- `id` を省略した場合は `id` を含めず、`params` を省略した場合は `params` を含めないこと
- `id` を省略した Notification には Sora がレスポンスを返さないこと
- E2E テスト (`test_sumomo_data_channel_signaling`) が通ること
- `python3 run.py iwyu ubuntu-24.04_x86_64` で差分が出ないこと
