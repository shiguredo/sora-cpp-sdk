# Jetson JetPack 6.1 に対応する

- Created: 2026-09-11
- Completed: {YYYY-MM-DD}
- Branch: feature/update-jetson-jetpack-6.1
- Polished: {YYYY-MM-DD}

## 目的

Sora C++ SDK の `support/jetson-jetpack-6` ブランチに JetPack 6.1 対応を追加する。

## 現状

- `support/jetson-jetpack-6` ブランチは JetPack 6.0.0 対応で、`VERSION` は `SORA_CPP_SDK_VERSION=2024.7.0-jetson-jetpack-6.0.0.0`、最終更新は 2024-07-31
- ブランチの README は "Ubuntu 22.04 ARMv8 Jetson (JetPack 6.0.0 以降)" を対象としている
- JetPack 6.1 は JetPack 6.0 の後継で、対応には libwebrtc (webrtc-build) の更新が必要
- 本 issue は C++ SDK 側の対応であり、Momo 側は別 issue で対応している

## 設計方針

- `support/jetson-jetpack-6` ブランチで、JetPack 6.1 の Jetson 実機でビルドと接続を確認する
- JetPack 6.1 に合わせて webrtc-build のバージョンを更新する。対応する libwebrtc のバージョンを確認する
- JetPack 6.1 で更新された GPU ドライバ・CUDA・マルチメディア API への影響を確認する
- 対応内容を `support/jetson-jetpack-6` ブランチに反映する

## 完了条件

- JetPack 6.1 の Jetson で Sora C++ SDK がビルドできること
- JetPack 6.1 の Jetson で Sora へ AV1 / H.265 などを送受信できること
- 対応内容が `support/jetson-jetpack-6` ブランチに反映されていること

## Pending 理由

- JetPack 6.1 の Jetson 実機が必要で、検証環境を用意できていない
- 対応する libwebrtc (webrtc-build) のバージョンと、JetPack 6.1 の GPU ドライバ・CUDA に依存する部分の確認が必要
- サポートブランチの更新であり、優先度が確定していない

## Pending 解除条件

- JetPack 6.1 の実機でビルドと接続を確認できる状態になったこと
