# iOS で video-orientation RTP ヘッダー拡張を有効時に映像を回転させると再エンコードが実行される

- Created: 2026-09-11
- Completed: {YYYY-MM-DD}
- Branch: feature/fix-ios-video-orientation-reencode
- Polished: {YYYY-MM-DD}

## 目的

video-orientation RTP ヘッダー拡張を有効にしたとき、iOS で画面を回転させてもエンコーダーを再生成せず、video-orientation で回転を伝えて映像を送信し続けられるようにする。Sora のデフォルトでは video-orientation は無効だが、有効にしても正しく動作するべきである。

## 現状

- video-orientation RTP ヘッダー拡張を有効にすると、answer SDP に `a=extmap:4 urn:3gpp:video-orientation` が含まれる
- 画面を回転させたときにエンコーダーの再生成が実行される。期待値はエンコーダーの再生成が行われず、映像が送信され続けること
- 受信側の映像は回転しているため、video-orientation が効いていない状態になっている
- 検証環境: iPhone 12 mini、iOS 26、Sora C++ SDK 2025.6.0-canary.5、hello アプリ
- `src` / `include` に video-orientation RTP ヘッダー拡張を扱うコードはない
- Sora のデフォルトは video-orientation が無効のため、利用者への影響は限定的

## 設計方針

- iOS の映像入力 (capturer) で回転を検出したときの処理を確認し、video-orientation RTP ヘッダー拡張が有効な場合はエンコーダーを再生成せずに回転情報を送る経路を検討する
- libwebrtc 側で video-orientation を有効にする設定と `webrtc::VideoFrame::set_rotation` の扱いを確認する
- 受信側の video-orientation の扱い (0096) と合わせて整理する

## 完了条件

- video-orientation を有効にしたとき、iOS で画面を回転させてもエンコーダーが再生成されないこと
- 受信側で回転が反映されること
- video-orientation が無効な既存挙動に回帰がないこと

## Pending 理由

- 修正には iOS 実機での確認が必要
- video-orientation の送受信の実装方針が未確定であり、受信側の対応 (0096) と合わせた設計判断が必要
- Sora のデフォルトでは video-orientation が無効であり、優先度が確定していない

## Pending 解除条件

- video-orientation の送受信の実装方針が決まり、iOS 実機で確認できる見込みが立ったこと
