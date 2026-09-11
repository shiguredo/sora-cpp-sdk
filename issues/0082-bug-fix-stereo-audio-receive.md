# ステレオ音声を受信できるようにする

- Created: 2026-09-11
- Completed: {YYYY-MM-DD}
- Branch: feature/fix-stereo-audio-receive
- Polished: {YYYY-MM-DD}

## 目的

recvonly でステレオ音声を受信し、左右のチャネルを保って再生できるようにする。現状は answer SDP に `stereo=1` が含まれず、受信した音声がモノラルで再生される。

## 現状

- Sora C++ SDK の recvonly の answer SDP の `a=fmtp:109` に `stereo=1` が含まれず、`a=fmtp:109 minptime=10;useinbandfec=1` となっている
- 実行コマンド: `./sumomo --signaling-url wss://sora.example.com/signaling --role recvonly --channel-id sora --use_sdl` (macOS)
- Chrome / Firefox でも同様に answer SDP に `stereo=1` が含まれない
- sora-js-sdk の check_stereo サンプルは左右の音声の diff からステレオ受信を判定しており、SDP の書き換えでステレオ受信を確認している
- Sora C++ SDK は `src/session_description.cpp` の `SessionDescription::CreateAnswer` で `webrtc::PeerConnectionInterface::CreateAnswer` の結果をそのまま `SetLocalDescription` しており、SDP を書き換えていない
- 対応するソース: `src/session_description.cpp`、`src/sora_signaling.cpp`

## 設計方針

- libwebrtc が answer SDP の opus の `a=fmtp` に `stereo=1` を付与する条件を確認する
- sora-js-sdk と同様に SDP を書き換える方法を検討する。書き換える場合は `SessionDescription::CreateAnswer` の `SetLocalDescription` 前が候補になる
- 受信側の再生がステレオになるかを左右差で確認する
- libwebrtc 側の対応が必要かを切り分ける

## 完了条件

- recvonly の answer SDP の opus に `stereo=1` が含まれること
- ステレオ音声を受信し、左右差を保って再生できること
- モノラルの既存挙動に回帰がないこと
