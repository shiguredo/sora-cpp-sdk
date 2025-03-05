# Sora C++ SDK

[![GitHub tag (latest SemVer)](https://img.shields.io/github/tag/shiguredo/sora-cpp-sdk.svg)](https://github.com/shiguredo/sora-cpp-sdk)
[![License](https://img.shields.io/badge/License-Apache%202.0-blue.svg)](https://opensource.org/licenses/Apache-2.0)

## About Shiguredo's open source software

We will not respond to PRs or issues that have not been discussed on Discord. Also, Discord is only available in Japanese.

Please read <https://github.com/shiguredo/oss/blob/master/README.en.md> before use.

## 時雨堂のオープンソースソフトウェアについて

利用前に <https://github.com/shiguredo/oss> をお読みください。

## Sora C++ SDK について

様々なプラットフォームに対応した [WebRTC SFU Sora](https://sora.shiguredo.jp/) 向けの C++ SDK です。

## 特徴

- 各プラットフォームで利用可能な HWA への対応
  - [Intel VPL](https://github.com/intel/libvpl)
    - AV1 / H.264 / H.265
      - VP9 のデコードは利用可能ですが、エンコードは現在既知の問題があります。詳細は [known_issues.md](doc/known_issues.md) をお読みください。
  - [NVIDIA Video Codec SDK](https://developer.nvidia.com/video-codec-sdk)
    - VP9 / H.264 / H.265
  - [NVIDIA JetPack SDK](https://developer.nvidia.com/embedded/jetpack) (JetPack 6)
    - VP9 / AV1 / H.264 / H.265
  - [Apple Video Toolbox](https://developer.apple.com/documentation/videotoolbox)
    - H.264 / H.265
  - Google Android HWA
    - VP8 / VP9 / H.264 / H.265
- [Cisco OpenH264](https://www.openh264.org/) への対応
  - Ubuntu x86_64,arm64 / macOS arm64 / Windows x86_64

## ライブラリのバイナリ提供について

以下からダウンロードが可能です。

_hololens2 は無視してください_

<https://github.com/shiguredo/sora-cpp-sdk/releases>

## サンプル集

[examples](examples)を参照してください。

## 対応 Sora

- WebRTC SFU Sora 2024.1.0 以降

## 動作環境

- Windows 10.1809 x86_64 以降
- macOS 14 arm64 以降
- Ubuntu 22.04 x86_64
- Ubuntu 24.04 x86_64
- Ubuntu 24.04 arm64
- Android 7 arm64 以降
- iOS 14 arm64 以降
- Ubuntu 22.04 ARMv8 Jetson (JetPack 6.0 以降)
  - Jetson AGX Orin
  - Jetson Orin NX
    - 動作未検証です

## 既知の問題

[known_issues.md](doc/known_issues.md) をお読みください。

## FAQ

[faq.md](doc/faq.md) をお読みください。

## メンテナンスポリシー

Sora C++ SDK のメンテナンスポリシーにはプライオリティがあります。

詳細については [maintenance_policy.md](doc/maintenance_policy.md) をお読みください。

## 優先実装

優先実装とは Sora のライセンスを契約頂いているお客様限定で Sora C++ SDK の実装予定機能を有償にて前倒しで実装することです。

- Intel VPL H.265 対応
  - [アダワープジャパン株式会社](https://adawarp.com/) 様

### 優先実装が可能な対応一覧

**詳細は Discord やメールなどでお気軽にお問い合わせください**

- NVIDIA Jetson JetPack 5 対応
- NVIDIA Jetson JetPack 6 Jetson Orin Nano 対応
- Raspberry Pi OS (64bit) arm64 対応
- Windows arm64 対応
- AMD AMD 対応

## サポートについて

### Discord

- **サポートしません**
- アドバイスします
- フィードバック歓迎します

最新の状況などは Discord で共有しています。質問や相談も Discord でのみ受け付けています。

<https://discord.gg/shiguredo>

### バグ報告

Discord へお願いします。

## ライセンス

Apache License 2.0

```text
Copyright 2021-2025, Wandbox LLC (Original Author)
Copyright 2021-2025, Shiguredo Inc.

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
```

## OpenH264

<https://www.openh264.org/BINARY_LICENSE.txt>

```text
"OpenH264 Video Codec provided by Cisco Systems, Inc."
```
