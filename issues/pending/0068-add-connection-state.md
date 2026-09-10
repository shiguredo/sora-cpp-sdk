# RTCPeerConnection の接続状態をアプリから参照できるようにする

- Created: 2026-09-10
- Completed: {YYYY-MM-DD}
- Branch: feature/add-connection-state
- Polished: {YYYY-MM-DD}

## 目的

Sora C++ SDK を利用するアプリが `RTCPeerConnection` の接続状態（`signalingState` / `iceGatheringState` / `iceConnectionState` / `connectionState`）を参照・検知できるようにする。接続断や接続失敗をアプリや上位 SDK が UI や再接続処理に反映できるようにするため。

## 現状

- `include/sora/sora_signaling.h` の `SoraSignaling::GetPeerConnection` は public であり、アプリは `webrtc::PeerConnectionInterface` を取得できる
- libwebrtc の `webrtc::PeerConnectionInterface` には `signaling_state()` / `ice_gathering_state()` / `ice_connection_state()` / `standardized_ice_connection_state()` / `peer_connection_state()` があり、アプリは `SoraSignaling::GetPeerConnection` 経由でポーリングすれば W3C の WebRTC 仕様にある 4 状態を取得できる
- `SoraSignalingObserver` には接続状態の変化を通知するコールバックがなく、状態変化を検知するにはポーリングが必要
- `src/sora_signaling.cpp` の `SoraSignaling::OnStandardizedIceConnectionChange` / `SoraSignaling::OnConnectionChange` は状態を `ice_state_` / `connection_state_` に保持するだけで、ログ出力と接続失敗（`PeerConnectionState::kFailed`）時の切断処理にのみ利用している。`SoraSignaling::OnSignalingChange` / `SoraSignaling::OnIceGatheringChange` は何もしない
- `SoraSignaling` の `ice_state_` / `connection_state_` を取得する getter はない
- 元になった sora-oss-private の issue では `RTCPeerConnectionFactory` をアプリに見せて `RTCPeerConnection` を自作・差し替えできるようにする案（`bypass_voice_processing` の設定変更などの用途）も挙がっているが、影響範囲が大きい
- `doc/` に接続状態の参照方法の記載はない

## 設計方針

- W3C の WebRTC 仕様の state definitions (https://www.w3.org/TR/webrtc/#state-definitions) にある 4 状態すべてを参照できるようにする
- 通知方法は次の候補を比較して決める
  - `SoraSignalingObserver` に状態変化コールバックを追加する
  - `SoraSignaling` に現在の状態を返す getter を追加する
  - 既存の `SoraSignaling::GetPeerConnection` をアプリが直接利用する運用とし、ドキュメントに記載する
- `SoraSignalingObserver` を変更する場合は既存の実装が壊れない形（デフォルト実装を持つ仮想関数など）にする
- 状態変化の通知は `boost::asio::post` で `SoraSignalingConfig::io_context` にポストし、スレッド安全性を保つ
- `RTCPeerConnectionFactory` の差し替えは本 issue の対象外とし、必要になった時点で別途検討する

## 完了条件

- アプリが 4 状態を参照できること
- 接続状態の変化を検知する手段が確定し、アプリから利用できること
- 既存の公開 API と上位 SDK の互換性が保たれること
- `CHANGES.md` の `## develop` に `[ADD]` エントリを追記していること

## Pending 理由

- 公開する状態の範囲と通知方法（コールバックを追加するか、getter を追加するか、既存の `GetPeerConnection` の利用で足りるか）が未確定である
- `RTCPeerConnection` を SDK としてどこまで公開するかは意図的に制限している部分があるため、仕様の検討から行う必要がある
- `SoraSignalingObserver` にコールバックを追加する場合は上位 SDK への影響確認が必要である

## Pending 解除条件

- 公開する状態の範囲と通知方法が確定したこと
- 上位 SDK への影響有無が確認できたこと
