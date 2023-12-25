# Sora C++ SDK サンプル集

[![License](https://img.shields.io/badge/License-Apache%202.0-blue.svg)](https://opensource.org/licenses/Apache-2.0)

このリポジトリには、 [Sora C++ SDK](https://github.com/shiguredo/sora-cpp-sdk) を利用したサンプルアプリを掲載しています。実際の利用シーンに即したサンプルをご用意しておりますので、目的に応じた Sora C++ SDK の使い方を簡単に学ぶことができます。

## About Shiguredo's open source software

We will not respond to PRs or issues that have not been discussed on Discord. Also, Discord is only available in Japanese.

Please read https://github.com/shiguredo/oss/blob/master/README.en.md before use.

## 時雨堂のオープンソースソフトウェアについて

利用前に https://github.com/shiguredo/oss をお読みください。

## 動作環境

- Windows 10 21H2 x86_64 以降
- macOS 12.4 arm64 以降
- Ubuntu 20.04 ARMv8 Jetson
    - [NVIDIA Jetson AGX Orin](https://www.nvidia.com/ja-jp/autonomous-machines/embedded-systems/jetson-orin/)
    - [NVIDIA Jetson Xavier NX](https://www.nvidia.com/ja-jp/autonomous-machines/embedded-systems/jetson-xavier-nx/)
    - [NVIDIA Jetson AGX Xavier](https://www.nvidia.com/ja-jp/autonomous-machines/embedded-systems/jetson-agx-xavier/)
- Ubuntu 22.04 x86_64
- Ubuntu 20.04 x86_64

## 対応 WebRTC SFU Sora

- WebRTC SFU Sora 2023.2.0 以降

## サンプルの紹介

### SDL サンプル

WebRTC SFU Sora と映像の送受信を行い、[SDL (Simple DirectMedia Layer)](https://www.libsdl.org/) を利用して受信映像を表示するサンプルです。
使い方は [SDL サンプルを使ってみる](./doc/USE_SDL_SAMPLE.md) をお読みください。

### Momo サンプル

[WebRTC Native Client Momo](https://github.com/shiguredo/momo) の sora モードを模したサンプルです。
使い方は [Momo サンプルを使ってみる](./doc/USE_MOMO_SAMPLE.md) をお読みください。

### メッセージング受信サンプル

WebRTC SFU Sora の [メッセージング機能](https://sora-doc.shiguredo.jp/MESSAGING) を使って送信されたメッセージを受信するサンプルです。
使い方は [メッセージング受信サンプルを使ってみる](./doc/USE_MESSAGING_RECVONLY_SAMPLE.md) をお読みください。

## ライセンス

Apache License 2.0

```
Copyright 2022-2023, Wandbox LLC (Original Author)
Copyright 2022-2023, Shiguredo Inc.

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
