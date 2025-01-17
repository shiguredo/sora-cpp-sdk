# Sumomo を使ってみる

## 概要

[WebRTC Native Client Momo](https://github.com/shiguredo/momo) の sora モードを模したサンプルです。

## 動作環境

[動作環境](../../README.md#動作環境) をご確認ください。

接続先として WebRTC SFU Sora サーバ ([Sora Labo](https://sora-labo.shiguredo.app/) / [Sora Cloud](https://sora-cloud.shiguredo.jp/) を含む) が必要です。[対応 Sora](../../README.md#対応-sora) もご確認ください。

## サンプルをビルドする

以下にそれぞれのプラットフォームでのビルド方法を記載します。

**ビルドに関しての問い合わせは受け付けておりません。うまくいかない場合は [GitHub Actions](https://github.com/shiguredo/sora-cpp-sdk/blob/develop/.github/workflows/build.yml) の内容をご確認ください。**

### リポジトリをクローンする

[sora-cpp-sdk](https://github.com/shiguredo/sora-cpp-sdk) をクローンして、examples 以下のディレクトリを利用してください。

develop ブランチは開発ブランチであり、ビルドが失敗することがあるため、 `main` またはリリースタグを指定するようにしてください。

以下は main ブランチを指定する例です。

```shell
git clone -b main https://github.com/shiguredo/sora-cpp-sdk.git
cd sora-cpp-sdk/examples
```

### Sumomo をビルドする

#### Windows x86_64 向けのビルドをする

##### 事前準備

以下のツールを準備してください。

- [Visual Studio 2022](https://visualstudio.microsoft.com/ja/downloads/)
  - C++ をビルドするためのコンポーネントを入れてください。
- Python 3.10.5

##### ビルド

```powershell
> python3 sumomo\windows_x86_64\run.py
```

成功した場合、`_build\windows_x86_64\release\sumomo\Release` に `sumomo.exe` が作成されます。

```
\_BUILD\WINDOWS_X86_64\RELEASE\sumomo\RELEASE
    sumomo.exe
```

#### macOS arm64 向けのビルドをする

##### 事前準備

以下のツールを準備してください。

- Python 3.12.4

##### ビルド

```shell
python3 sumomo/macos_arm64/run.py
```

成功した場合、`_build/macos_arm64/release/sumomo` に `sumomo` が作成されます。

```
_build/macos_arm64/release/sumomo
└── sumomo
```

#### Ubuntu 20.04 x86_64 向けのビルドをする

##### 事前準備

必要なパッケージをインストールしてください。

```shell
sudo apt install build-essential
sudo apt install libxext-dev
sudo apt install libx11-dev
sudo apt install pkg-config
sudo apt install python3
```

##### ビルド

```shell
python3 sumomo/ubuntu-20.04_x86_64/run.py
```

成功した場合、`_build/ubuntu-20.04_x86_64/release/sumomo` に `sumomo` が作成されます。

```
_build/ubuntu-20.04_x86_64/release/sumomo/
└── sumomo
```

#### Ubuntu 22.04 x86_64 向けのビルドをする

##### 事前準備

必要なパッケージをインストールしてください。

```shell
sudo apt install build-essential
sudo apt install libxext-dev
sudo apt install libx11-dev
sudo apt install pkg-config
sudo apt install python3
```

##### ビルド

```shell
python3 sumomo/ubuntu-22.04_x86_64/run.py
```

成功した場合、以下のファイルが作成されます。`_build/ubuntu-22.04_x86_64/release/sumomo` に `sumomo` が作成されます。

```
_build/ubuntu-22.04_x86_64/release/sumomo/
└── sumomo
```

#### Ubuntu 24.04 x86_64 向けのビルドをする

##### 事前準備

必要なパッケージをインストールしてください。

```shell
sudo apt install build-essential
sudo apt install libxext-dev
sudo apt install libx11-dev
sudo apt install pkg-config
sudo apt install python3
```

##### ビルド

```shell
python3 sumomo/ubuntu-24.04_x86_64/run.py
```

成功した場合、以下のファイルが作成されます。`_build/ubuntu-24.04_x86_64/release/sumomo` に `sumomo` が作成されます。

```
_build/ubuntu-24.04_x86_64/release/sumomo/
└── sumomo
```

## 実行する

### コマンドラインから必要なオプションを指定して実行します

ビルドされたバイナリのあるディレクトリに移動して、コマンドラインから必要なオプションを指定して実行します。
以下は Sora サーバのシグナリング URL `wss://sora.example.com/signaling` の `sora` チャンネルに `sendrecv` で接続する例です。

Windows の場合

```powershell
> .\sumomo.exe --signaling-url wss://sora.example.com/signaling --role sendrecv --channel-id sora --multistream true --use-sdl
```

Windows 以外の場合

```shell
./sumomo --signaling-url wss://sora.example.com/signaling --role sendrecv --channel-id sora --multistream true --use-sdl
```

#### 必須オプション

- `--signaling-url` : Sora サーバのシグナリング URL (必須)
- `--channel-id` : [channel_id](https://sora-doc.shiguredo.jp/SIGNALING#ee30e9) (必須)
- `--role` : [role](https://sora-doc.shiguredo.jp/SIGNALING#6d21b9) (必須)
  - sendrecv / sendonly / recvonly のいずれかを指定

#### Sumomo 実行に関するオプション

- `--log-level` : 実行時にターミナルに出力するログのレベル
  - `verbose`,`info`,`warning`,`error`,`none` の値が指定可能です
- `--resolution` : 映像配信する際の解像度
  - 解像度は `QVGA, VGA, HD, FHD, 4K, or [WIDTH]x[HEIGHT]` の値が指定可能です
  - 未指定の場合は `VGA` が設定されます
- `--hw-mjpeg-decoder` : HW MJPEG デコーダーの利用 (true/false)
  - 未指定の場合は false が設定されます
  - NVIDIA Jetson のみで利用できます
- `--use-hardware-encoder` : ハードウェアエンコーダーの利用 (true/false)
- `--openh264` : openh264 ライブラリのパスをフルパスで指定します
  - デコードには対応していません

#### Sora に関するオプション

設定内容については [Sora のドキュメント](https://sora-doc.shiguredo.jp/SIGNALING) も参考にしてください。

- `--client-id` : [client_id](https://sora-doc.shiguredo.jp/SIGNALING#d00933)
- `--video` : 映像の利用 (true/false)
  - 未指定の場合は true が設定されます
- `--audio` : 音声の利用 (true/false)
  - 未指定の場合は true が設定されます
- `--video-codec-type` : [ビデオコーデック指定](https://sora-doc.shiguredo.jp/SIGNALING#d47f4d)
  - VP8 / VP9 / AV1 / H264 / H265 が指定可能ですが利用可能なコーデックはプラットフォームに依存します
  - 未指定の場合は Sora のデフォルトである VP9 が利用されます
- `--audio-codec-type` : [オーディオコーデック指定](https://sora-doc.shiguredo.jp/SIGNALING#0fcf4e)
  - OPUS が指定可能です
  - 未指定の場合は Sora のデフォルトである OPUS が利用されます
- `--video-bit-rate` : [ビデオビットレート指定](https://sora-doc.shiguredo.jp/SIGNALING#5667cf)
  - 0 - 30000 の値が指定可能です
  - 0 は未指定と見なされます
- `--video-h264-params` : [ビデオの H.264 設定指定](https://sora-doc.shiguredo.jp/SIGNALING#ffc4cb)
- `--video-h265-params` : [ビデオの H.265 設定指定](https://sora-doc.shiguredo.jp/SIGNALING#bfe45b)
- `--audio-bit-rate` : [オーディオビットレート指定](https://sora-doc.shiguredo.jp/SIGNALING#414142)
  - 0 - 510 の値が指定可能です
  - 0 は未指定と見なされます
- `--metadata` : [メタデータ](https://sora-doc.shiguredo.jp/SIGNALING#414142)
  - JSON 形式の文字列を指定してください
- `--multistream` : [マルチストリーム](https://sora-doc.shiguredo.jp/SIGNALING#808bc2) 機能の利用 (true/false)
  - 未指定の場合は Sora の設定 (デフォルト: true) が設定されます
- `--spotlight` : [スポットライト](https://sora-doc.shiguredo.jp/SIGNALING#8f6c79) 機能の利用 (true/false)
  - 未指定の場合は Sora の設定 (デフォルト: false) が設定されます
- `--spotlight-number` : [spotlight_number](https://sora-doc.shiguredo.jp/SPOTLIGHT#c66032)
  - 0 - 8 の値が指定可能です
  - 0 は未指定と見なされます
- `--simulcast` : [サイマルキャスト](https://sora-doc.shiguredo.jp/SIGNALING#584185) 機能の利用 (true/false)
  - 未指定の場合は Sora の設定 (デフォルト: false) が設定されます
- `--data-channel-signaling` : [DataChannel 経由のシグナリング](https://sora-doc.shiguredo.jp/DATA_CHANNEL_SIGNALING) を行います (true/false)
  - 未指定の場合は Sora の設定 (デフォルト: false) が設定されます
- `--ignore-disconnect-websocket`
  - 未指定の場合は Sora の設定 (デフォルト: false) が設定されます

#### proxy に関するオプション

- `--proxy-url` : プロキシの url
  - 例) <http://proxy.example.com:3128>
- `--proxy-username` : プロキシ認証に使用するユーザー名
- `--proxy-password` : プロキシ認証に使用するパスワード

#### SDL に関するオプション

- `--use-sdl`
  - [SDL (Simple DirectMedia Layer)](https://www.libsdl.org/) を利用して映像を表示します
- `--window-width`
  - 映像を表示するウインドウの横幅を指定します
- `--window-height`
  - 映像を表示するウインドウの縦幅を指定します
- `--fullscreen`
  - 映像を表示するウインドウをフルスクリーンにします
- `--show-me`
  - 送信している自分の映像を表示します

#### 証明書に関するオプション

- `--insecure` : サーバー証明書の検証を行わないようにするフラグ
  - 未指定の場合は、サーバー証明書の検証を行います
- `--ca-cert` : CA 証明書ファイル
- `--client-cert` : クライアント証明書ファイル
- `--client-key` : クライアントプライベートキーファイル

`--ca-cert`, `--client-cert`, `--client-key` には、PEM 形式のファイルを指定してください。

#### 映像と音声のデバイスに関するオプション

- `--video-device`
  - 映像デバイスの名前を指定します
- `--audio-recording-device`
  - 音声録音デバイスの名前を指定します
- `--audio-playout-device`
  - 音声再生デバイスの名前を指定します

デバイスの名前はプラットフォームごとに確認する一般的な方法か、`log-level` オプションを `info` にして実行することでログに出力されます。

#### 映像品質の維持優先度に関するオプション

- `--degradation-preference`
  - `disabled`, `maintain_framerate`,`maintain_resolution`, `balanced` が指定可能です。
  - 設定可能な値の詳細は [ W3C のドキュメント](https://www.w3.org/TR/mst-content-hint/#degradation-preference-when-encoding) を参照してください。

#### その他のオプション

- `--help`
  - ヘルプを表示します
