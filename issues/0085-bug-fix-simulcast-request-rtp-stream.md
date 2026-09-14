# Intel VPL 環境でサイマルキャスト送信時に RequestSimulcastRid API で解像度が切り替わらない

- Created: 2026-09-11
- Completed: {YYYY-MM-DD}
- Branch: feature/fix-simulcast-request-rid
- Polished: 2026-09-14

## 目的

AV1 サイマルキャストを送信している接続で、Sora の HTTP API (`RequestSimulcastRid`) を実行しても受信側の解像度が切り替わらない事象を調査し、C++ SDK 側で対処が必要な場合は修正する。E2E テストで確認できる見込みがある。

## 現状

### 発生事象

- Ubuntu 24.04.1 の Intel VPL 環境 (Intel Core Ultra 5 125H、Intel iHD driver 24.1.0) で AV1 サイマルキャストを送信し、受信側の受信ストリームを切り替える API を実行しても、受信側の解像度が切り替わらない
- 事象を最初に確認した際に使用した API は非推奨の `RequestRtpStream` (`Sora_20201005.RequestRtpStream`) である。この機能は現行の Sora (2025.12 以降) で正式 API の `RequestSimulcastRid` (`Sora_20251217.RequestSimulcastRid`) に置き換わっており、`RequestRtpStream` は 2027 年 12 月リリース予定の Sora で廃止される。本 issue の対応では `RequestSimulcastRid` を使う
- Sora C++ SDK は `SoraSignalingConfig::simulcast_request_rid` を持ち、`src/sora_signaling.cpp` の connect メッセージで接続時に `simulcast_request_rid` を Sora へ送る。しかし接続後に受信する rid を切り替える手段は Sora サーバーの HTTP API (`api_port`、既定 3000) のみであり、SDK には該当 API を呼び出す仕組みも切り替え結果を確認する仕組みもない
- `examples/sumomo` に `simulcast_request_rid` を指定するオプションはない (サイマルキャスト関連では `--simulcast` のみ)
- `e2e-test/` に Sora の HTTP API を実行して解像度を確認するテストはない。`e2e-test/conftest.py` の `SoraSettings` は signaling URL / channel ID prefix / secret key / channel ID / metadata のみで、Sora の API を呼ぶ仕組みを持っていない
- 同一環境の AV1 受信で映像が緑になる事象は、`intel-media-va-driver-non-free` の導入で解消済みであり、本 issue は rid 切り替えに限定する

### 再現手順

1. `intel-media-va-driver-non-free` を導入した Ubuntu 24.04 の Intel VPL 環境で、送信側の sumomo を起動する (`--role sendonly --simulcast true --video-codec-type AV1 --av1-encoder intel_vpl --resolution 960x540 --video-bit-rate 3000`)
2. 同じチャネル ID で受信側の sumomo を起動する (`--role recvonly --simulcast true --video-codec-type AV1`)。受信側が `simulcast_request_rid` を指定しない場合、Sora は `r0` を配信するため、受信側の `inbound-rtp` は 240x128 になる
3. 受信側が映像を受信し始めたら、Sora の HTTP API に `RequestSimulcastRid` (POST /、ヘッダー `x-sora-target: Sora_20251217.RequestSimulcastRid`) を `channel_id` / `receiver_connection_id` (受信側) / `sender_connection_id` (送信側) / `rid: "r2"` で送る。接続 ID は `ListChannelConnections` (`Sora_20201013.ListChannelConnections`) で `client_id` から特定する
4. 受信側の `http://<host>:<port>/stats` から `inbound-rtp` の `frameWidth` / `frameHeight` を確認する

期待する挙動は `r0` の 240x128 から `r2` の 960x528 へ変化することである。

### 実行ログと環境

- 実行ログ: https://gist.github.com/torikizi/d7ce636b1970091e192f594d713ed258
  - 2024-10-09 収集、Sora C++ SDK 2024.8.0-canary.13 / libwebrtc M129.6668 / Sora 2024.2.0-canary.30 のログである
  - このログは AV1 サイマルキャストの接続と送受信の確認用で、Sora の API の実行や解像度の切り替えの成否は含まない

## 設計方針

- E2E テストで Sora の HTTP API `RequestSimulcastRid` を実行し、受信側の `inbound-rtp` の `frameWidth` / `frameHeight` が切り替わるかを確認できるようにする。E2E の設定に Sora API の URL (既定の `api_port` は 3000) を追加する必要がある。Sora の HTTP API に認証機能は現在のドキュメントに記載がないため、E2E で用意するのは API を呼べる URL と、`ListChannelConnections` で接続 ID を取得する手段であり、API の認証は Sora 側の設定に依存する
- 切り替わらない場合、Sora が切り替え時に受信側へ通知を行っているかも含め、送信側・受信側のどちらに原因があるかを切り分ける
- Sora 側の挙動や libwebrtc 側の対応が必要かを確認する
- 再現に使った環境 (Ubuntu 24.04.1 / Intel Core Ultra / iHD driver のバージョン) と実行ログを issue に記録する

## 完了条件

- E2E テストで `RequestSimulcastRid` による解像度の切り替えが確認できること。テストは `e2e-test/test_sumomo_intel_vpl.py` に追加し、`INTEL_VPL=1` で実行できること。送信 960x540 の AV1 では、受信側の `inbound-rtp` が `r0` の 240x128 から `r2` の 960x528 へ変化することを確認する
- 切り替わらない場合、原因が特定され、SDK 側の修正または Sora / libwebrtc 側の対応要否が判断されていること
- 通常のサイマルキャスト送信に回帰がないこと (既存の `e2e-test/test_sumomo_intel_vpl.py` の `test_simulcast` が通ること)
