## 開発時の方針

- 公開するヘッダは `include/sora/` 以下に入れる
- 公開しないヘッダと実装は `src/` 以下に入れる
- 公開するヘッダに外部依存として含めていいのは以下のライブラリだけにして、他のライブラリ（CUDA や NvCodec）は実装に隠して利用する
  - 各 OS のデフォルトで入っているライブラリ( `windows.h` や `sys/ioctl.h` など)
  - Boost
  - WebRTC
- ビルドフラグによらず常にビルドされるファイルは、サブディレクトリを作らず `include/sora/` と `src/` に保存する
- ビルドフラグによってビルドされたりされなかったりするファイルは、サブディレクトリを作ってそこに保存する

## メモ

- ubuntu-20.04_x86_64 に必要な依存
  - clang-10
  - CUDA
```bash
sudo apt-get update
sudo apt-get install -y software-properties-common
wget https://developer.download.nvidia.com/compute/cuda/repos/ubuntu1804/x86_64/cuda-ubuntu1804.pin
sudo mv cuda-ubuntu1804.pin /etc/apt/preferences.d/cuda-repository-pin-600
sudo apt-key adv --fetch-keys https://developer.download.nvidia.com/compute/cuda/repos/ubuntu1804/x86_64/7fa2af80.pub
sudo add-apt-repository "deb https://developer.download.nvidia.com/compute/cuda/repos/ubuntu1804/x86_64/ /"
sudo apt-get update
# 10.2.89-1 の部分は VERSION ファイルの CUDA_VERSION を参照すること
sudo apt-get -y install cuda=10.2.89-1

# 以下は CUDA 11 以上でしか使えない
# wget https://developer.download.nvidia.com/compute/cuda/repos/ubuntu2004/x86_64/cuda-ubuntu2004.pin
# sudo mv cuda-ubuntu2004.pin /etc/apt/preferences.d/cuda-repository-pin-600
# sudo apt-key adv --fetch-keys https://developer.download.nvidia.com/compute/cuda/repos/ubuntu2004/x86_64/3bf863cc.pub
# sudo add-apt-repository "deb https://developer.download.nvidia.com/compute/cuda/repos/ubuntu2004/x86_64/ /"
# sudo apt-get update
# sudo apt-get -y install cuda=10.2 clang-10
```