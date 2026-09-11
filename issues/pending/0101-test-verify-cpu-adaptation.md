# cpu adaptation が効いているか判断する方法を確認する

- Created: 2026-09-11
- Completed: {YYYY-MM-DD}
- Branch: feature/verify-cpu-adaptation
- Polished: {YYYY-MM-DD}

## 目的

Sora C++ SDK の CPU adaptation (`SoraSignalingConfig::cpu_adaptation`) が実際に効いているかを判断する方法を確認する。

## 現状

- `src/sora_signaling.cpp` の `CreatePeerConnection` で `config_.cpu_adaptation` の値に応じて `webrtc::PeerConnectionInterface::RTCConfiguration::set_cpu_adaptation` を設定している
- macOS のサイマルキャスト時は、解像度が無限に落ちる問題を回避するために `cpu_adaptation` を無効化している
- libwebrtc のログ (`webrtc_video_engine.cc` の `VideoSendStream stats`) の `cpu_adapted_res` / `cpu_adapted_fps` / `#cpu_adaptations` は、CPU adaptation を有効にしても変化しない
- 統計情報に `outbound-rtp` の値が追加されたため、そこから CPU adaptation が効いているかを判断できる見込みがある
- CPU リソースが足りない状況を再現できておらず、動作検証ができていない

## 設計方針

- `outbound-rtp` の統計情報から CPU adaptation の状態を判断できる項目を確認する
- CPU リソースが足りない状況 (負荷の高いエンコード設定、CPU の制限など) を再現し、`cpu_adaptation` の有無で解像度 / フレームレートが変化するかを確認する
- 判断方法を issue またはドキュメントに記録する

## 完了条件

- CPU adaptation が効いているかを判断する方法が確認されていること
- `cpu_adaptation` を有効 / 無効にしたときの差が確認されていること
- 確認方法が issue またはドキュメントに記録されていること

## Pending 理由

- CPU リソースが足りない状況を再現できておらず、動作検証の方法が確立していない
- `outbound-rtp` のどの項目で判断できるかの確認が必要
- libwebrtc の挙動に依存するため、確認に時間がかかる

## Pending 解除条件

- CPU リソースが足りない状況を再現でき、CPU adaptation の効果を確認できる見込みが立ったこと
