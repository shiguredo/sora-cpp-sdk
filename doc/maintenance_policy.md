# メンテナンスポリシー

Sora C++ SDK ではメンテナンスポリシーにはプライオリティがあります。

プライオリティは P1 から P4 まであります。
P1 が一番プライオリティが高いです。

## プライオリティ

- P1 と P2 は ``develop`` ブランチで開発します
- P1 と P2 ``main`` ブランチでリリースされます
- P1 と P2 ``main`` ブランチでタグを打ちます
- P1 と P2 は [Sora Unity SDK](https://github.com/shiguredo/sora-unity-sdk) で利用できます
- P1 と P2 は [Sora Python SDK](https://github.com/shiguredo/sora-python-sdk) で利用できます
- P1 は最新の OS や HWA への対応が C++ SDK のリリースのトリガーになる場合があります
- P2 は最新の OS や HWA への対応が C++ SDK のリリースのトリガーになりません
- P3 と P4 は C++ SDK メジャーリリースには含まれません
- P3 と P4 は ``support/`` ブランチでタグを打ちます
- P1 と P2 と P3 は Sora のメジャーリリースに追従します
- P4 は優先実装でのみ ``main`` ブランチへ追従します
- P4 は優先実装でのみアップデートを行います

## 独自タグについて

- P1 と P2 以外はそれぞれで独自タグを利用します
- タグの一番最後はリリース回数
- リリース回数以外の文字列が変化したらリリース回数は 0 にもどす
- `{sora-cpp-sdk-version}-{platform-name}-{platform-version}.{release}`
- `2024.1.0-jetson-jetpack-5.1.3.0`
- `2024.1.0-jetson-jetpack-5.1.3.1`
- `2024.1.0-jetson-jetpack-5.1.3.2`
- `2024.1.0-jetson-jetpack-5.1.4.0`
- `2024.1.0-jetson-jetpack-5.1.4.1`
- `2024.1.0-jetson-jetpack-5.1.4.2`
- `2024.1.1-jetson-jetpack-5.1.4.0`
- `2024.1.1-jetson-jetpack-5.1.4.1`

## P1

- Apple macOS / iOS / iPadOS
- Google Android

### 方針

- `develop` ブランチで開発
- `main` ブランチでリリース
- 最新の OS がリリースされたタイミングで対応
- 最新のハードウェアアクセラレーターに対応
- Sora メジャーリリースへ追従
- リリースのタイミングで検証

## P2

- Intel VPL x86_64
  - Ubuntu 24.04
  - Ubuntu 22.04
  - Windows 11
- NVIDIA Video Codec SDK x86_64
  - Ubuntu 24.04
  - Ubuntu 22.04
  - Windows 11

### 方針

- `develop` ブランチで開発
- `main` ブランチでリリース
- 最新の OS がリリースされたタイミングでは対応しない
- 最新のハードウェアアクセラレーターに対応しない
- Sora メジャーリリースへの追従
- リリースのタイミングでの検証は最低限

## P3

- NVIDIA Jetson JetPack 6 arm64
  - ブランチ: `support/jetson-jetpack-6`
  - タグ: `{sora-cpp-sdk-version}-jetson-jetpack-{platform-version}.{release}`
    - `2024.1.0-jetson-jetpack-6.0.0.0`
- Microsoft HoloLens 2
  - ブランチ: `support/hololens2`
  - タグ: `{sora-cpp-sdk-version}-hololens2.{release}`
    - `2024.1.0-hololens2.0`
- Raspberry Pi OS
  - ブランチ: `support/raspberry-pi`
  - タグ: `{sora-cpp-sdk-version}-raspberry-pi.{release}`
    - `2024.1.0-raspberry-pi.0`

### 方針

- ``support`` ブランチで開発
- 独自タグでリリースされる
- Sora メジャーリリースに追従する
- リリースのタイミングでの検証は最低限

## P4

- NVIDIA Jetson JetPack 5 arm64
  - ブランチ: `support/jetson-jetpack-5`
  - タグ: `{sora-cpp-sdk-version}-jetson-jetpack-{platform-version}.{release}`
    - `2024.1.0-jetson-jetpack-5.1.3.0`

### 方針

- ``support`` ブランチで開発
- 独自タグでリリースされる
- Sora メジャーリリースに追従しない
- リリースのタイミングでの検証は最低限
- 優先実装でのみ Sora メジャーリリースへ追従
- 優先実装でのみアップデートを行う
