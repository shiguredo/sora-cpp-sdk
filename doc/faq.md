# FAQ

## ドキュメント

Sora C++ SDK にドキュメントはありません。Sora C++ SDK サンプル集を参考にしてください。

不明点などあれば時雨堂の Discord <https://discord.gg/shiguredo> の `#sora-sdk-faq` にてご相談ください。

## ビルド

様々なプラットフォームへ対応しているため、ビルドがとても複雑です。

ビルド関連の質問については環境問題がほとんどであり、環境問題を改善するコストがとても高いため基本的には回答いたしません。

GitHub Actions でビルドを行い確認していますので、まずは GitHub Actions の [build.yml](https://github.com/shiguredo/sora-cpp-sdk/blob/develop/.github/workflows/build.yml) を確認してみてください。

GitHub Actions のビルドが失敗していたり、
ビルド済みバイナリがうまく動作しない場合は Discord へご連絡ください。

## NVIDIA Jetson Orin (Ubuntu 22.04 arm64) でビルドできません

Ubuntu 22.04 x86_64 でクロスコンパイルしたバイナリを利用するようにしてください。

## NVIDIA 搭載の Windows で width height のいずれかが 128 未満のサイズの VP9 の映像を受信できません

NVIDIA Video Codec のハードウェアデコーダでは width または height のいずれかが 128 未満である場合、 VP9 の映像をデコードできません。width または height のいずれかが 128 未満のサイズの映像を受信したい場合は、 VP9 以外のコーデックを利用するようにしてください。

## Windows の Chrome で Jetson の H.264 映像を受信すると色が緑色になります

Windows 環境でのみ Jetson の H.264 映像を受信すると映像の色が緑色になってしまうことを確認しています。こちらの事象は NVIDIA Jetson の JetPack を 5.1.1 にすることで解決します。

## iOS または macOS から H.264 の 1080p で配信するにはどうすればいいですか？

iOS または macOS から 1080p で配信したい場合は Sora の H.264 のプロファイルレベル ID を 5.2 以上に設定してください。
設定は以下の方法で行うことができます。

- `SoraSignalingConfig` に `video_h264_params` を設定する
- Sora の設定を変更する

Sora の設定については [Sora のドキュメント](https://sora-doc.shiguredo.jp/SORA_CONF#1581db) をご確認ください。

## 4K@30fps で映像を配信できません

Sora C++ SDK は 4K@30fps での映像の配信に対応していますが、
一部の環境では 4K@30fps で映像を配信できない場合があります。

その場合、 `SoraVideoEncoderFactoryConfig` という構造体の `force_i420_conversion` フラグを `false` にすることで、4K@30fps で映像を配信できる場合があります。

かなり内部的な話なので詳細については [コードのコメント](https://github.com/shiguredo/sora-cpp-sdk/blob/c29d3d6ec721ddc3d625275296506750a63460d4/include/sora/sora_video_encoder_factory.h#L59-L73) をご確認ください。

## AMD AMF で映像受信時に映像の停止やデコード失敗が発生する

AMD AMF を使用して映像を受信する際に、映像の停止やデコード失敗が発生することがあります。
その場合は、AMD のグラフィックスドライバーのバージョンを更新することで解決できる可能性があります。
[AMD のサポートページ](https://www.amd.com/ja/support/download/drivers.html)から最新のドライバーをダウンロードしてインストールした後、映像の受信を試してみてください。

## Raspberry Pi で libcamera や libcamera control を使用したい

Raspberry Pi で USB カメラではなく Raspberry Pi Camera を使用する場合、libcamera を有効にする必要があります。
libcamera を有効にするには `sora::CameraDeviceCapturerConfig` で `use_libcamera` フラグを true に指定します。
また、実行ファイルと同じディレクトリに `libcamera.so` を置いておく必要があります。

libcamera を有効にすると sora::CameraDeviceCapturerConfig に libcamera に関する以下のオプションを指定できます

- ネイティブバッファを使うための `libcamera_native_frame_output` フラグ
  - ただし、HWA の H.264 エンコーダを使う場合にのみ有効です
- libcamera コントロールを使うための `libcamera_controls` オプション
  - `{"AfMode", "Continuous"}` のようなキーと値のペアを必要な分だけ追加します

## Raspberry Pi で H.264 の FHD をデコードすると緑の線が入る

Raspberry Pi OS の v4l2m2m では解像度が 16 の倍数である必要があります。
FHD (1920x1080) では、1080 が 16 の倍数ではないため、緑の線が入ります。
送信側の解像度を 1920x1072 や 1920x1088 にして 16 の倍数になるように調整するか、
H.264 以外のコーデックを使用してください。

## Windows のノートパソコンに搭載されているマイクで音声送信ができません

Windows のノートパソコンで搭載されているマイクデバイスは、マイクのチャンネル数が 4 ch の場合があります。
Sora C++ SDK は 2 ch までのマイクのみをサポートしているため。4 ch マイクの場合音声送信ができません。
別途 2ch 以下のマイクを用意していただく必要があります。

こちらでは ROG Flow X13、ROG Zephyrus G16 に搭載されているマイクが 4ch であるために音声送信ができない事象を確認しています。

マイクのチャネル数は Windows 11 の場合以下の手順で確認ください
1. 「設定」 → 「システム」 → 「サウンド」
2. 「入力」欄から利用するマイクを選択
3. 「入力設定」の「形式」からチャネル数を確認