# FAQ

## ビルド

ビルド関連の質問については環境問題がほとんどであり、環境問題を改善するコストがとても高いため基本的には解答しません。

GitHub Actions でビルドを行い確認していますので、まずは GitHub Actions の [build.yml](https://github.com/shiguredo/sora-cpp-sdk/blob/develop/.github/workflows/build.yml) を確認してみてください。

GitHub Actions のビルドが失敗していたり、
ビルド済みバイナリがうまく動作しない場合は Discord へご連絡ください。

### NVIDIA Jetson Orin (Ubuntu 20.04 arm64) でビルドできません

Ubuntu 20.04 x86_64 でクロスコンパイルしたバイナリを利用するようにしてください。
