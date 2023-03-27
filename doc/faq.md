# FAQ

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

Chrome のハードウェアアクセラレーションによって、 Windows 環境でのみ Jetson の H.264 映像を受信すると映像の色が緑色になってしまうことを確認しています。解決策として Chrome の ハードウェアアクセラレーションを無効にすることで、映像の色が緑色になるのを回避できます。設定方法を以下に記載します。

`Chrome の設定 -> システム -> ハードウェア アクセラレーションが使用可能な場合は使用する` の順で Chrome の設定ページを開き、スクリーンショットの赤枠の部分をOFF に切り替えて Chrome を再起動してください。

[![Image from Gyazo](https://i.gyazo.com/15fd370b7b21c4e0990e9516a8981840.png)](https://gyazo.com/15fd370b7b21c4e0990e9516a8981840)