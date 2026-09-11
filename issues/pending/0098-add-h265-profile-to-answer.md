# H.265 利用時に Answer に Profile を含める

- Created: 2026-09-11
- Completed: {YYYY-MM-DD}
- Branch: feature/add-h265-profile-to-answer
- Polished: {YYYY-MM-DD}

## 目的

Sora C++ SDK で H.265 を利用するとき、Answer SDP の `a=fmtp` に Profile を含める。ブラウザの H.265 実装と同等の SDP にする。

## 現状

- ブラウザの H.265 の SDP は `a=fmtp:120 level-id=93;profile-id=1;tier-flag=0;tx-mode=SRST` のように Profile を含む
- Sora C++ SDK の H.265 の Answer には Profile が含まれない
- H.265 の SDP に関する関連 issue がある
- Chrome が実現しているため対応を検討しているが、実現が難しい場合は対応しない判断もありうる

## 設計方針

- H.265 の SDP の `a=fmtp` に `profile-id` / `level-id` / `tier-flag` / `tx-mode` を追加する方法を検討する
- libwebrtc の H.265 対応 (webrtc-build の h265.patch) と SDP 生成のどこで Profile を設定できるかを確認する
- Sora / 受信側で Profile が必須かどうかを確認する
- 実現が難しい場合は、その理由と判断を issue に記録してクローズする

## 完了条件

- H.265 の Answer SDP に Profile が含まれること
- ブラウザと同等の H.265 の SDP になること
- 実現しない場合は、その理由と判断が issue に記録されていること

## Pending 理由

- libwebrtc の H.265 対応が webrtc-build のパッチに依存しており、SDP 生成のどこで Profile を設定できるかの確認が必要
- Chrome の実装を参考にできるが、C++ SDK / libwebrtc で実現可能かが未確定
- 関連する issue の状況を確認する必要がある

## Pending 解除条件

- H.265 の Profile を設定する方法が判明し、実装または対応しない判断ができる状態になったこと
