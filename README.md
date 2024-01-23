# Sora C++ SDK

[![GitHub tag (latest SemVer)](https://img.shields.io/github/tag/shiguredo/sora-cpp-sdk.svg)](https://github.com/shiguredo/sora-cpp-sdk)
[![License](https://img.shields.io/badge/License-Apache%202.0-blue.svg)](https://opensource.org/licenses/Apache-2.0)

## About Shiguredo's open source software

We will not respond to PRs or issues that have not been discussed on Discord. Also, Discord is only available in Japanese.

Please read https://github.com/shiguredo/oss/blob/master/README.en.md before use.

## 時雨堂のオープンソースソフトウェアについて

利用前に https://github.com/shiguredo/oss をお読みください。

## Sora C++ SDK について

様々なプラットフォームに対応した [WebRTC SFU Sora](https://sora.shiguredo.jp/) 向けの C++ SDK です。

## 特徴

- 各プラットフォームで利用可能な HWA への対応
  - NVIDIA VIDEO CODEC SDK (NVENC / NVDEC)
    - VP9 / H.264
  - NVIDIA Jetson Video HWA
    - VP9 / AV1 / H.264
  - Apple macOS / iOS Video Toolbox
    - H.264 / H.265
  - Google Android HWA
    - VP8 / VP9 / H.264 / H.265
  - [Intel oneVPL](https://www.intel.com/content/www/us/en/developer/tools/oneapi/onevpl.html) (Intel Media SDK の後継)
    - VP9 / AV1 / H.264
- 各プラットフォームへの [google/lyra: A Very Low\-Bitrate Codec for Speech Compression](https://github.com/google/lyra) 対応

## ライブラリのバイナリ提供について

以下からダウンロードが可能です。

_hololens2 は無視してください_

https://github.com/shiguredo/sora-cpp-sdk/releases

## 対応 Sora

- WebRTC SFU Sora 2023.2.0 以降

## 動作環境

- Windows 10.1809 x86_64 以降
- macOS 13.5 arm64 以降
- Ubuntu 20.04 ARMv8 Jetson (JetPack 5.1.1 以降)
  - Jetson AGX Orin
  - Jetson AGX Xavier
  - Jetson Xavier NX
- Ubuntu 20.04 x86_64
- Ubuntu 22.04 x86_64
- Android 7 arm64 以降
- iOS 13 arm64 以降

## サンプル集

https://github.com/shiguredo/sora-cpp-sdk-samples

## 使ってみる

準備中。

## FAQ

[faq.md](doc/faq.md) をお読みください。

## 優先実装

優先実装とは Sora のライセンスを契約頂いているお客様限定で Sora C++ SDK の実装予定機能を有償にて前倒しで実装することです。

### 優先実装が可能な機能一覧

**詳細は Discord やメールなどでお気軽にお問い合わせください**

- NVIDIA Jetson JetPack 6 対応
- AMD 系 HWA 対応
- Windows arm64 対応

## サポートについて

### Discord

- **サポートしません**
- アドバイスします
- フィードバック歓迎します

最新の状況などは Discord で共有しています。質問や相談も Discord でのみ受け付けています。

https://discord.gg/shiguredo

### バグ報告

Discord へお願いします。

## ライセンス

Apache License 2.0

```
Copyright 2021-2024, Wandbox LLC (Original Author)
Copyright 2021-2024, Shiguredo Inc.

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
