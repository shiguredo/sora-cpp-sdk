# video-orientation RTP ヘッダー拡張を有効時に受信した映像が回転しない

- Created: 2026-09-11
- Completed: {YYYY-MM-DD}
- Branch: feature/fix-video-orientation-receive
- Polished: {YYYY-MM-DD}

## 目的

video-orientation RTP ヘッダー拡張を有効にしたとき、受信した映像を回転して描画できるようにする。Sora のデフォルトでは video-orientation は無効だが、有効にしても正しく動作するべきである。

## 現状

- Sora の設定で `rtp_hdrext_video_orientation = true` にしても、sumomo で受信した映像が回転しない
- sumomo の answer SDP に `a=extmap:4 urn:3gpp:video-orientation` は含まれる
- Chrome で受信すると回転する
- 検証環境: macOS 26.0.1、sumomo Sora C++ SDK 2025.5.1、送信側 Pixel 6 (Android 16) + Sora Android SDK 2025.3.0-canary.0
- `src/renderer/base_renderer.cpp` は `webrtc::VideoFrame::rotation()` を見て回転を扱うが、受信フレームに rotation が設定されていない
- Sora のデフォルトは video-orientation が無効のため、利用者への影響は限定的

## 設計方針

- libwebrtc が video-orientation RTP ヘッダー拡張から `webrtc::VideoFrame::rotation()` を設定する条件を確認する
- 受信側で rotation が設定されない原因 (RTP ヘッダー拡張のネゴシエーション、libwebrtc の設定) を切り分ける
- レンダラー (`src/renderer/base_renderer.cpp`) で回転が反映されることを確認する
- 送信側の video-orientation の扱い (0094) と合わせて整理する

## 完了条件

- video-orientation を有効にしたとき、受信した映像が回転して描画されること
- video-orientation が無効な既存挙動に回帰がないこと

## Pending 理由

- libwebrtc 側で video-orientation がどのように処理されるかの確認が必要
- 送信側の対応 (0094) と合わせた実装方針の設計判断が必要
- Sora のデフォルトでは video-orientation が無効であり、優先度が確定していない

## Pending 解除条件

- video-orientation の送受信の実装方針が決まり、確認できる見込みが立ったこと
