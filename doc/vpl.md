# Intel VPL

## 概要

ここでは Intel VPL のハードウェアアクセラレーター機能を利用した、
エンコーダーとデコーダーを利用する方法について説明します。

## Intel VPL ランタイムについて

Intel VPL には Intel VPL (ライブラリの Intel VPL と区別するために、以後は Intel VPL ランタイムと表記します) と Intel Media SDK の 2 つのランタイムがあり、チップの世代によって利用できるランタイムが異なります。

第 11 世代 以降のチップを利用している場合は、 Intel VPL ランタイムを利用することができます。

Intel Media SDK は既に開発が終了しており、後継の Intel VPL ランタイムに開発が移行しているため、
これから VPL を利用する場合は、 Intel VPL ランタイムに対応したチップを利用することを推奨します。

<https://www.intel.com/content/www/us/en/developer/tools/vpl/overview.html#gs.73uoi4> の Specifications のセクションより、ランタイムと対応するチップの一覧を以下に引用します。

- [Intel VPL](https://github.com/oneapi-src/oneVPL-intel-gpu)
  - Intel® Iris® Xe graphics
  - Intel Iris Xe MAX graphics
  - Intel® Arc™ Graphics
  - Intel Data Center GPU Flex Series
  - 11th generation Intel® Core™ processors and newer using integrated graphics
- [Intel Media SDK](https://github.com/Intel-Media-SDK/MediaSDK)
  - Intel® Server GPU
  - 5th to 11th generation Intel Core processors using integrated graphics

## 環境構築

### Windows

Windows 11 では Intel の公式サイトからドライバーをインストールすることで VPL を利用することができます。

- Intel の公式サイトからドライバーをダウンロードします。
  - Intel ドライバーおよびソフトウェアのダウンロード
    - <https://www.intel.co.jp/content/www/jp/ja/download-center/home.html>
- インストーラーに従ってインストールを行います。
- インストール後に再起動を行います。

### Ubuntu 22.04

Ubuntu 22.04 で Intel VPL を利用するためには、ドライバーとライブラリをインストールする必要があります。
公式ドキュメントの <https://dgpu-docs.intel.com/driver/client/overview.html> を参考に必要なドライバーとライブラリをインストールします。

#### Intel VPL ランタイムをインストールする

##### Intel の apt リポジトリを追加

パッケージのインストールには Intel の apt リポジトリを追加する必要があります。

```bash

wget -qO - https://repositories.intel.com/gpu/intel-graphics.key | \
  sudo gpg --dearmor --output /usr/share/keyrings/intel-graphics.gpg
echo "deb [arch=amd64,i386 signed-by=/usr/share/keyrings/intel-graphics.gpg] https://repositories.intel.com/gpu/ubuntu jammy client" | \
  sudo tee /etc/apt/sources.list.d/intel-gpu-jammy.list
sudo apt update
```

##### パッケージのインストール

公式ドキュメントでは libmfxgen1 をインストールする手順が記載されていますが、Intel VPL ランタイムを使用するには libmfx-gen1.2 が必要です。

以下の実行例のように、 libmfx-gen1.2 をインストールしてください。

```bash
sudo apt install -y \
  intel-opencl-icd intel-level-zero-gpu level-zero \
  intel-media-va-driver-non-free libmfx1 libmfx-gen1.2 libvpl2 \
  libegl-mesa0 libegl1-mesa libegl1-mesa-dev libgbm1 libgl1-mesa-dev libgl1-mesa-dri \
  libglapi-mesa libgles2-mesa-dev libglx-mesa0 libigdgmm12 libxatracker2 mesa-va-drivers \
  mesa-vdpau-drivers mesa-vulkan-drivers va-driver-all vainfo hwinfo clinfo
```

### Ubuntu 24.04

Ubuntu 24.04 で Intel VPL を利用するためには、ライブラリをインストールする必要があります。
デコードのみであれば標準のリポジトリからも libmfx-gen1.2 をインストール可能ですが、エンコードも行いたいため Intel の apt リポジトリより libmfxgen1 をインストールします。

#### Intel VPL ランタイムをインストールする

##### Intel の apt リポジトリを追加

パッケージのインストールには Intel の apt リポジトリを追加する必要があります。

```bash

wget -qO - https://repositories.intel.com/gpu/intel-graphics.key | \
  sudo gpg --dearmor --output /usr/share/keyrings/intel-graphics.gpg
echo "deb [arch=amd64,i386 signed-by=/usr/share/keyrings/intel-graphics.gpg] https://repositories.intel.com/gpu/ubuntu noble client" | \
  sudo tee /etc/apt/sources.list.d/intel-gpu-noble.list
sudo apt update
```

##### パッケージのインストール

以下の実行例のように、 libmfxgen1 をインストールしてください。

```bash
sudo apt install -y libmfxgen1
```

##### 再起動

パッケージのインストールが完了したら、再起動してください。

#### Intel Media SDK を利用する手順

Intel のチップセットの世代によって、 Intel Media SDK を利用する必要がある場合があります。

以下の手順で Intel Media SDK をインストールしてください。

##### Intel の apt リポジトリを追加

```bash
wget -qO - https://repositories.intel.com/gpu/intel-graphics.key | \
  sudo gpg --dearmor --output /usr/share/keyrings/intel-graphics.gpg
echo "deb [arch=amd64,i386 signed-by=/usr/share/keyrings/intel-graphics.gpg] https://repositories.intel.com/gpu/ubuntu jammy client" | \
  sudo tee /etc/apt/sources.list.d/intel-gpu-jammy.list
sudo apt update
```

###### パッケージのインストール

```bash
sudo apt install -y \
  intel-opencl-icd intel-level-zero-gpu level-zero \
  intel-media-va-driver-non-free libmfx1 libmfxgen1 libvpl2 \
  libegl-mesa0 libegl1-mesa libegl1-mesa-dev libgbm1 libgl1-mesa-dev libgl1-mesa-dri \
  libglapi-mesa libgles2-mesa-dev libglx-mesa0 libigdgmm12 libxatracker2 mesa-va-drivers \
  mesa-vdpau-drivers mesa-vulkan-drivers va-driver-all vainfo hwinfo clinfo
```

### Ubuntu 22.04 で環境構築ができたことを確認する手順

`vainfo` コマンドを実行します。  
エラーが発生しなければ、 Intel VPL の実行に必要なドライバーやライブラリのインストールに成功しています。

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
