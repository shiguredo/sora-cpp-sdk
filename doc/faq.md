# FAQ

## ドキュメント

Sora C++ SDK にドキュメントはありません。基本的にはサンプルを参考にしてください。

また、不明点などあれば Discord でお問い合わせください。

## ビルド

ビルド関連の質問については環境問題がほとんどであり、環境問題を改善するコストがとても高いため基本的には解答しません。

GitHub Actions でビルドを行い確認していますので、まずは GitHub Actions の [build.yml](https://github.com/shiguredo/sora-cpp-sdk/blob/develop/.github/workflows/build.yml) を確認してみてください。

GitHub Actions のビルドが失敗していたり、
ビルド済みバイナリがうまく動作しない場合は Discord へご連絡ください。

## NVIDIA Jetson Orin (Ubuntu 20.04 arm64) でビルドできません

Ubuntu 20.04 x86_64 でクロスコンパイルしたバイナリを利用するようにしてください。

## NVIDIA 搭載の Windows で width height のいずれかが 128 未満のサイズの VP9 の映像を受信できません

NVIDIA VIDEO CODEC SDK のハードウェアデコーダでは width height のいずれかが 128 未満である場合 VP9 の映像をデコードできません。 width height のいずれかが 128 未満のサイズの映像を受信したい場合は VP9 以外のコーデックを利用するようにしてください。

## Windows の Chrome で Jetson の H.264 映像を受信すると色が緑色になります

Windows 環境でのみ Jetson の H.264 映像を受信すると映像の色が緑色になってしまうことを確認しています。こちらの事象は NVIDIA Jetson の JetPack を 5.1.1 にすることで解決します。

## iOS または macOS から H.264 の FHD で配信するにはどうすればいいですか？

iOS または macOS から FHD で配信したい場合は Sora の H.264 のプロファイルレベル ID を 5.2 以上に設定してください。設定は以下の方法で行うことができます。

- `SoraSignalingConfig` に `video_h264_params` を設定する
- Sora の設定を変更する

Sora の設定については [Sora のドキュメント](https://sora-doc.shiguredo.jp/SORA_CONF#1581db) をご確認ください。
