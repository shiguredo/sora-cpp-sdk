# Intel Media SDK

## 概要

ここでは Intel Media SDK のハードウェアアクセラレーター機能を利用した、
エンコーダーとデコーダーを利用する方法について説明します。

### Intel Media SDK を利用する手順

Intel の第 11 世代以前の世代をご利用の場合、 Intel VPL には対応していないため Intel Media SDK を利用する必要があります。

以下の手順で Intel Media SDK をインストールしてください。

### Windows での環境構築

Windows では Windows Update によりドライバーが提供され利用することが可能です。

しかし、最新版のドライバーが提供されていない場合があるため、Intel の公式サイトからドライバーをダウンロードしてインストールすることをお勧めします。

- インテル® ドライバー & サポート・アシスタント を利用する方法
  インテル® ドライバー & サポート・アシスタントを利用することで、自動的に環境に合った最新のドライバーをダウンロードしてインストールすることができます。
  - Intel の公式サイトからインテル® ドライバー & サポート・アシスタントをダウンロードします。
    - インテル® ドライバー & サポート・アシスタント
      - <https://www.intel.co.jp/content/www/jp/ja/support/detect.html>
  - インストーラーに従ってインストールを行います。
  - インストール後に再起動を行います。

### Ubuntu での環境構築

#### Intel の apt リポジトリを追加

```bash
wget -qO - https://repositories.intel.com/gpu/intel-graphics.key | \
  sudo gpg --dearmor --output /usr/share/keyrings/intel-graphics.gpg
echo "deb [arch=amd64,i386 signed-by=/usr/share/keyrings/intel-graphics.gpg] https://repositories.intel.com/gpu/ubuntu jammy client" | \
  sudo tee /etc/apt/sources.list.d/intel-gpu-jammy.list
sudo apt update
```

##### パッケージのインストール

```bash
sudo apt install -y \
  intel-opencl-icd intel-level-zero-gpu level-zero \
  intel-media-va-driver-non-free libmfx1 libmfxgen1 libvpl2 \
  libegl-mesa0 libegl1-mesa libegl1-mesa-dev libgbm1 libgl1-mesa-dev libgl1-mesa-dri \
  libglapi-mesa libgles2-mesa-dev libglx-mesa0 libigdgmm12 libxatracker2 mesa-va-drivers \
  mesa-vdpau-drivers mesa-vulkan-drivers va-driver-all vainfo hwinfo clinfo
```
