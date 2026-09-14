# ステレオ音声を受信できるようにする

- Created: 2026-09-11
- Completed: {YYYY-MM-DD}
- Branch: feature/fix-stereo-audio-receive
- Polished: 2026-09-14

## 目的

recvonly でステレオ音声を受信し、左右のチャネルを保って再生できるようにする。現状は answer SDP に `stereo=1` が含まれず、受信した音声がモノラルで再生される。

## 現状

- Sora C++ SDK の recvonly の answer SDP の `a=fmtp:109` に `stereo=1` が含まれず、`a=fmtp:109 minptime=10;useinbandfec=1` となっている
- 実行コマンド: `./sumomo --signaling-url wss://sora.example.com/signaling --role recvonly --channel-id sora --use-sdl` (macOS)
- Chrome / Firefox でも同様に answer SDP に `stereo=1` が含まれない
- sora-js-sdk の e2e-tests/fake_stereo_audio は、受信音声の左右チャネルの支配周波数の差からステレオ受信を判定しており、`forceStereoOutput` による answer SDP の書き換えでステレオ受信を確認している
- Sora C++ SDK は `src/session_description.cpp` の `SessionDescription::CreateAnswer` で `webrtc::PeerConnectionInterface::CreateAnswer` の結果をそのまま `SetLocalDescription` しており、SDP を書き換えていない
- 対応するソース: `src/session_description.cpp`、`src/sora_signaling.cpp`

## 設計方針

- libwebrtc が answer SDP の opus の `a=fmtp` に `stereo=1` を付与する条件を確認する
- sora-js-sdk と同様に SDP を書き換える方法を検討する。書き換える場合は `SessionDescription::CreateAnswer` の `SetLocalDescription` 前が候補になる
- 受信側の再生がステレオになるかを左右差で確認する。検証には左右差のあるステレオ音声を送信するクライアントが必要であり、sora-js-sdk の fake_stereo_audio が該当する。C++ SDK / sumomo 側で左右差を判定する手段は別 issue (0089) で用意する
- libwebrtc 側の対応が必要かを切り分ける

## 完了条件

- recvonly の answer SDP の opus に `stereo=1` が含まれること
- ステレオ音声を受信し、左右差を保って再生できること (左右差の確認は 0089 の判定手段で行う)
- モノラルの既存挙動に回帰がないこと
