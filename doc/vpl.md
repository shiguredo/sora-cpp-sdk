# Intel VPL

## 概要

ここでは Intel VPL のハードウェアアクセラレーター機能を利用した、
エンコーダーとデコーダーを利用する方法について説明します。

## Intel VPL ランタイムについて

Intel VPL (ライブラリの Intel VPL と区別するために、以後は Intel VPL ランタイムと表記します) は第 11 世代以降のチップで利用することができます。

Intel VPL が利用できるチップセットは、Intel の公式サイトから、<https://www.intel.com/content/www/us/en/developer/tools/vpl/overview.html#gs.73uoi4> の Specifications のセクションに記載されています。

## 環境構築

### Windows

Windows では Windows Update によりドライバーが提供され利用することが可能です。

しかし、最新版のドライバーが提供されていない場合があるため、Intel の公式サイトからドライバーをダウンロードしてインストールすることをお勧めします。

Intel の公式サイトからドライバーをダウンロードする方法は 2 種類あります。

1. インテル® ドライバー & サポート・アシスタント を利用する方法
  インテル® ドライバー & サポート・アシスタントを利用することで、自動的に環境に合った最新のドライバーをダウンロードしてインストールすることができます。
  - Intel の公式サイトからインテル® ドライバー & サポート・アシスタントをダウンロードします。
    - インテル® ドライバー & サポート・アシスタント
      - <https://www.intel.co.jp/content/www/jp/ja/support/detect.html>
  - インストーラーに従ってインストールを行います。
  - インストール後に再起動を行います。

2. インテルの公式サイトから直接ダウンロードする方法
  すでに環境で必要なドライバーがわかっている場合は、直接ダウンロードしてインストールすることができます。
  - Intel の公式サイトから直接ドライバーをダウンロードします。
    - インテル® Arc™ & Iris® Xe Graphics - Windows*
      - <https://www.intel.co.jp/content/www/jp/ja/download/785597/intel-arc-iris-xe-graphics-windows.html>
  - ダウンロードしたファイルを実行し、インストールを行います。
  - インストール後に再起動を行います。

### Ubuntu 22.04

#### Intel の apt リポジトリを追加

ランタイムのインストールには Intel の apt リポジトリを追加する必要があります。

```bash
wget -qO - https://repositories.intel.com/gpu/intel-graphics.key | \
  sudo gpg --dearmor --output /usr/share/keyrings/intel-graphics.gpg
echo "deb [arch=amd64,i386 signed-by=/usr/share/keyrings/intel-graphics.gpg] https://repositories.intel.com/gpu/ubuntu jammy client" | \
  sudo tee /etc/apt/sources.list.d/intel-gpu-jammy.list
sudo apt update
```

#### Intel 提供パッケージの最新化

Intel の apt リポジトリを追加することでインストール済みのパッケージも Intel から提供されている最新のものに更新できます。依存問題を起こさないため、ここで最新化を行なってください。

```bash
sudo apt upgrade
```

#### ドライバとライブラリのインストール

以下のように、ドライバとライブラリをインストールしてください。
intel-media-va-driver には無印と `non-free` 版がありますが、 `non-free` 版でしか動作しません。

```bash
sudo apt install -y intel-media-va-driver-non-free libmfxgen1
```

### Ubuntu 24.04

デコードのみであれば標準のリポジトリからも libmfx-gen1.2 をインストール可能ですが、エンコードも行いたいため Intel の apt リポジトリより libmfxgen1 をインストールします。

#### Intel の apt リポジトリを追加

ランタイムのインストールには Intel の apt リポジトリを追加する必要があります。

```bash

wget -qO - https://repositories.intel.com/gpu/intel-graphics.key | \
  sudo gpg --dearmor --output /usr/share/keyrings/intel-graphics.gpg
echo "deb [arch=amd64,i386 signed-by=/usr/share/keyrings/intel-graphics.gpg] https://repositories.intel.com/gpu/ubuntu noble client" | \
  sudo tee /etc/apt/sources.list.d/intel-gpu-noble.list
sudo apt update
```

#### ライブラリのインストール

以下の実行例のように、 libmfxgen1 をインストールしてください。

```bash
sudo apt install -y libmfxgen1
```

## Ubuntu 22.04 で環境構築ができたことを確認する手順

`vainfo` コマンドを実行します。  
エラーが発生しなければ、 Intel VPL の実行に必要なドライバーやライブラリのインストールに成功しています。

`vainfo` がインストールされていない場合は、下記のコマンドで `vainfo` をインストールします。

```bash
sudo apt install -y vainfo
```

以下は `vainfo` を実行した出力の例です。  
対応しているプロファイルやエントリーポイントは環境によって異なります。

```console
$ vainfo
Trying display: wayland
libva info: VA-API version 1.20.0
libva info: Trying to open /usr/lib/x86_64-linux-gnu/dri/iHD_drv_video.so
libva info: Found init function __vaDriverInit_1_20
libva info: va_openDriver() returns 0
vainfo: VA-API version: 1.20 (libva 2.20.0)
vainfo: Driver version: Intel iHD driver for Intel(R) Gen Graphics - 23.4.0 ()
vainfo: Supported profile and entrypoints
      VAProfileNone                   : VAEntrypointVideoProc
      VAProfileNone                   : VAEntrypointStats
      VAProfileMPEG2Simple            : VAEntrypointVLD
      VAProfileMPEG2Simple            : VAEntrypointEncSlice
      VAProfileMPEG2Main              : VAEntrypointVLD
      VAProfileMPEG2Main              : VAEntrypointEncSlice
...
```

## Sora C++ SDK で Intel VPL が利用されていることを確認する

WebRTC の統計情報から利用されているエンコーダー/デコーダーを確認できます。  
以下の値に `libvpl` と出力されている場合、 Intel VPL が利用されています。

- type: outbound-rtp の encoderImplementation
- type: inbound-rtp の decoderImplementation
