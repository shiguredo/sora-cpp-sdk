## 開発時の方針

- 公開するヘッダは `include/sora/` 以下に入れる
- 公開しないヘッダと実装は `src/` 以下に入れる
- 公開するヘッダに外部依存として含めていいのは以下のライブラリだけにして、他のライブラリ（CUDA や NvCodec）は実装に隠して利用する
  - 各 OS のデフォルトで入っているライブラリ( `windows.h` や `sys/ioctl.h` など)
  - Boost
  - WebRTC
- ビルドフラグによらず常にビルドされるファイルは、サブディレクトリを作らず `include/sora/` と `src/` に保存する
- ビルドフラグによってビルドされたりされなかったりするファイルは、サブディレクトリを作ってそこに保存する

## ローカルの webrtc-build を利用する

ローカルの webrtc-build を使ってビルドするには、以下のようにする。

```bash
# ../webrtc-build に shiguredo-webrtc-build/webrtc-build がある場合
python3 run.py ubuntu-20.04_x86_64 --webrtc-build-dir ../webrtc-build
```

webrtc-build に引数を渡してビルドする場合、以下のようにする。

```bash
python3 run.py ubuntu-20.04_x86_64 --webrtc-build-dir ../webrtc-build --webrtc-build-args='--webrtc-fetch'
```

この時、VERSION に指定している WEBRTC_BUILD_VERSION に関係なく、現在 webrtc-build リポジトリでチェックアウトされている内容でビルドするため、バージョンの不整合に注意すること。

## デバッグビルド

C++ SDK をデバッグビルドするには、libwebrtc も含めて、依存ライブラリすべてをデバッグビルドする必要がある。
しかし libwebrtc のバイナリはリリースビルドであるため、libwebrtc のデバッグバイナリを作るにはローカルの webrtc-build を利用する必要がある。

```bash
python3 run.py ubuntu-20.04_x86_64 --debug --webrtc-build-dir ../webrtc-build
```

このように `--debug` を付けると、C++ SDK だけでなく、ローカルの webrtc-build を含む全ての依存ライブラリもデバッグビルドを行う。

## メモ

### ubuntu-20.04_x86_64, ubuntu-22.04_x86_64 のビルドに必要な依存

- clang-18
- CUDA
```bash
# clang-18
wget https://apt.llvm.org/llvm.sh
chmod a+x llvm.sh
sudo ./llvm.sh 18

# CUDA
wget https://developer.download.nvidia.com/compute/cuda/repos/ubuntu2004/x86_64/cuda-keyring_1.0-1_all.deb
sudo dpkg -i cuda-keyring_*all.deb
sudo apt-get update
# 11.8.0-1 の部分は VERSION ファイルの CUDA_VERSION を参照すること
sudo apt-get -y install cuda=11.8.0-1
```
- libva-dev
- libdrm-dev
```bash
sudo apt install libva-dev libdrm-dev
```

### ubuntu-20.04_x86_64, ubuntu-22.04_x86_64 の実行に必要な依存

- libva2
- libdrm2
```bash
sudo apt install libva2 libdrm2
```
- （Intel VPL の Intel Media SDK を利用したいなら）libmfx1
- （Intel VPL の Intel VPL ランタイムを利用したいなら）libmfx-gen1.2 （Ubuntu 22.04 のみ利用可）
