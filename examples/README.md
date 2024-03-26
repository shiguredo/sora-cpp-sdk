# Sora C++ SDK サンプル集

examples 以下のディレクトリには、 Sora C++ SDK を利用したサンプルアプリを掲載しています。実際の利用シーンに即したサンプルをご用意しておりますので、目的に応じた Sora C++ SDK の使い方を簡単に学ぶことができます。

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

### Sumomo 

[WebRTC Native Client Momo](https://github.com/shiguredo/momo) の sora モードを模したサンプルです。
使い方は [Sumomo を使ってみる](./doc/USE_SUMOMO.md) をお読みください。

### メッセージング受信サンプル

WebRTC SFU Sora の [メッセージング機能](https://sora-doc.shiguredo.jp/MESSAGING) を使って送信されたメッセージを受信するサンプルです。
使い方は [メッセージング受信サンプルを使ってみる](./doc/USE_MESSAGING_RECVONLY_SAMPLE.md) をお読みください。
