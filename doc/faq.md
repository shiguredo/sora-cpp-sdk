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

## 4K@30fps で映像を配信できません

Sora C++ SDK では、 `SoraVideoEncoderFactoryConfig` という構造体に `force_i420_conversion_for_simulcast_adapter` というフラグがあり、デフォルトで true になっています。
このフラグを false にすることで、パフォーマンスが向上して 4K@30fps で映像を配信できる可能性がありますが、 JetsonBuffer を利用している環境などでサイマルキャストが正常に動作しなくなります。

このフラグが必要になった背景をコードの [コメント](https://github.com/shiguredo/sora-cpp-sdk/blob/8f6dba9218e0cda7cdefafe64a37c1af9d5e5c9e/include/sora/sora_video_encoder_factory.h#L57-L71) から以下に抜粋します。

```
このフラグが必要になった背景は以下の通り
- サイマルキャスト時、 JetsonBuffer のような一部の kNative なバッファーの実装において、バッファーを複数回読み込めないという制限があるため、 I420 への変換が必要になる
  - サイマルキャストは複数ストリームを送信するため、バッファーを複数回読みこむ必要がある
- サイマルキャスト時は use_simulcast_adapter = true にしてサイマルキャストアダプタを利用する必要があるが、 SoraClientContext の実装ではサイマルキャスト時でも非サイマルキャスト時でも常に use_simulcast_adapter = true として SoraVideoEncoderFactory を生成している
  - Sora に type: connect で simulcast 有効/無効を指定して接続しても、Sora 接続後に受信する type: offer でサイマルキャストの有効/無効が上書きできるため、SoraClientContext 生成時に use_simulcast_adapter の値をどうするべきか決定できない。そのため常に use_simulcast_adapter = true にしておくのが安全となる
    - 非サイマルキャスト時に use_simulcast_adapter = true とするのは、パフォーマンスの問題はあっても動作に影響は無い
- 上記の２つの問題によって、非サイマルキャスト時でも I420 への変換処理が走ることになって、解像度や性能によってはフレームレートが出ないことがある
- この I420 への変換は、 Sora の設定も含めて利用者が非サイマルキャストだと保証できる場合、あるいはサイマルキャストであっても複数回読める kNative なバッファーを利用している場合には不要な処理になる
- そのような場合に I420 への変換を無効にすることで十分なフレームレートを確保できるようにするために、このフラグが必要になった
```