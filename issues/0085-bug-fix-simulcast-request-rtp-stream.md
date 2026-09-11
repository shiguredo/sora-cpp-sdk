# VPL 環境でサイマルキャスト送信時に RequestRtpStream API で解像度が切り替わらない

- Created: 2026-09-11
- Completed: {YYYY-MM-DD}
- Branch: feature/fix-simulcast-request-rtp-stream
- Polished: {YYYY-MM-DD}

## 目的

AV1 サイマルキャストを送信している接続で `RequestRtpStream` API を実行しても、受信側の解像度が切り替わらない事象を調査し、C++ SDK 側で対処が必要な場合は修正する。E2E テストで確認できる見込みがある。

## 現状

- Ubuntu 24.04.1 の Intel VPL 環境 (Intel Core Ultra 5 125H、Intel iHD driver 24.1.0) で AV1 サイマルキャストを送信し、rid 切り替えの API (`RequestRtpStream`) を実行しても解像度が切り替わらない
- Sora C++ SDK は `SoraSignalingConfig::simulcast_request_rid` を持ち、`src/sora_signaling.cpp` で接続時に `simulcast_request_rid` を Sora へ送る。しかし接続後に rid を切り替える `RequestRtpStream` API の呼び出しや、切り替えを確認する仕組みは SDK にない
- `examples/sumomo` に `simulcast_request_rid` を指定するオプションはない
- `e2e-test/` に `RequestRtpStream` API を実行して解像度を確認するテストはない。`e2e-test/conftest.py` の `SoraSettings` は signaling URL / channel ID / secret key / metadata のみで、Sora API を呼ぶ仕組みを持っていない
- 同一環境の AV1 受信で映像が緑になる事象は、`intel-media-va-driver-non-free` の導入で解消済みであり、本 issue は rid 切り替えに限定する
- 実行ログは https://gist.github.com/torikizi/d7ce636b1970091e192f594d713ed258

## 設計方針

- E2E テストで Sora API の `RequestRtpStream` を実行し、受信側の解像度 (`inbound-rtp` の `frameWidth` / `frameHeight`) が切り替わるかを確認できるようにする。E2E の設定に Sora API の URL と認証を追加する必要がある
- 切り替わらない場合、SDK が Sora からの通知 (`notify`) を処理していないなど、送信側・受信側のどちらに原因があるかを切り分ける
- Sora 側の挙動や libwebrtc 側の対応が必要かを確認する
- 再現に使った環境 (Ubuntu 24.04.1 / Intel Core Ultra / iHD driver のバージョン) と実行ログを issue に記録する

## 完了条件

- E2E テストで `RequestRtpStream` による解像度の切り替えが確認できること
- 切り替わらない場合、原因が特定され、SDK 側の修正または Sora / libwebrtc 側の対応要否が判断されていること
- 通常のサイマルキャスト送信に回帰がないこと
