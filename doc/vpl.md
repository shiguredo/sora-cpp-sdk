# Intel VPL

## このドキュメントについて

Intel VPL を利用する方法を説明します

## 前提知識: ランタイムについて

Intel VPL には Intel VPL (ライブラリの Intel VPL と区別するために、以後は Intel VPL ランタイムと表記します) と Intel Media SDK の 2 つのランタイムがあり、チップの世代によって利用できるランタイムが異なります。

第 11 世代 以降のチップを利用している場合は、 Intel VPL ランタイムを利用することができます。

Intel Media SDK は既に開発が終了しており、後継の Intel VPL ランタイムに開発が移行しているため、これから VPL を利用する場合は、 Intel VPL ランタイムに対応したチップを利用することを推奨します。

https://www.intel.com/content/www/us/en/developer/tools/vpl/overview.html#gs.73uoi4 の Specifications のセクションより、ランタイムと対応するチップの一覧を以下に引用します。

- [Intel VPL](https://github.com/oneapi-src/oneVPL-intel-gpu)
  - Intel® Iris® Xe graphics
  - Intel Iris Xe MAX graphics
  - Intel® Arc™ Graphics
  - Intel Data Center GPU Flex Series
  - 11th generation Intel® Core™ processors and newer using integrated graphics
- [Intel Media SDK](https://github.com/Intel-Media-SDK/MediaSDK)
  - Intel® Server GPU
  - 5th to 11th generation Intel Core processors using integrated graphics

## 注意点: NVIDIA の GPU が搭載された PC で Intel VPL を利用する方法

Sora C++ SDK の 実装 (SoraVideoEncoderFactory, SoraVideoDecoderFactory クラス) では、 NVIDIA の GPU を利用するエンコーダー/デコーダーの優先度が Intel VPL を利用するものより高くなっています。

そのため、 NVIDIA の GPU が搭載された PC で Intel VPL を利用するには、以下のいずれかの対応が必要です。

- NVIDIA の GPU を利用するエンコーダー/デコーダーをビルド時に無効化する ... ビルド・スクリプトで `USE_NVCODEC_ENCODER` を指定している箇所を削除する
- NVIDIA の GPU のドライバーを削除する

Sora C++ SDK をビルドしている場合は、前者の方法を推奨します。  
また、 GPU のドライバーを削除する場合は自己責任で行ってください。

## 環境構築

### Windows

手動でドライバーのインストールなどを行う必要はありません。

### Ubuntu 22.04

https://dgpu-docs.intel.com/driver/client/overview.html を参考に必要なドライバーとソフトウェアをインストールします。

#### Intel VPL ランタイムを利用する手順

Intel VPL ランタイムを利用する場合は libmfxgen1 ではなく libmfx-gen1.2 を使う必要があるため、ドキュメントのコマンドを一部読み替えて実行します。

```bash
# Intel の apt リポジトリを追加
$ wget -qO - https://repositories.intel.com/gpu/intel-graphics.key | \
  sudo gpg --dearmor --output /usr/share/keyrings/intel-graphics.gpg
$ echo "deb [arch=amd64,i386 signed-by=/usr/share/keyrings/intel-graphics.gpg] https://repositories.intel.com/gpu/ubuntu jammy client" | \
  sudo tee /etc/apt/sources.list.d/intel-gpu-jammy.list
$ sudo apt update

# パッケージのインストール
$ sudo apt install -y \
  intel-opencl-icd intel-level-zero-gpu level-zero \
  intel-media-va-driver-non-free libmfx1 libmfx-gen1.2 libvpl2 \
  libegl-mesa0 libegl1-mesa libegl1-mesa-dev libgbm1 libgl1-mesa-dev libgl1-mesa-dri \
  libglapi-mesa libgles2-mesa-dev libglx-mesa0 libigdgmm12 libxatracker2 mesa-va-drivers \
  mesa-vdpau-drivers mesa-vulkan-drivers va-driver-all vainfo hwinfo clinfo
```

#### Intel Media SDK を利用する手順

ドキュメントの通りです。

```bash
# Intel の apt リポジトリを追加
$ wget -qO - https://repositories.intel.com/gpu/intel-graphics.key | \
  sudo gpg --dearmor --output /usr/share/keyrings/intel-graphics.gpg
$ echo "deb [arch=amd64,i386 signed-by=/usr/share/keyrings/intel-graphics.gpg] https://repositories.intel.com/gpu/ubuntu jammy client" | \
  sudo tee /etc/apt/sources.list.d/intel-gpu-jammy.list
$ sudo apt update

# パッケージのインストール
$ sudo apt install -y \
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

```
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
