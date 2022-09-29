# Momo サンプルを使ってみる

## 概要

[WebRTC Native Client Momo](https://github.com/shiguredo/momo) の sora モードを模したサンプルです。

## 動作環境

[動作環境](../README.md#動作環境) をご確認ください。

接続先として WebRTC SFU Sora サーバ ([Sora Labo](https://sora-labo.shiguredo.app/) / [Tobi](https://tobi.shiguredo.jp/) を含む) が必要です。[対応 Sora](../README.md#対応-sora) もご確認ください。

## サンプルをビルドする

以下にそれぞれのプラットフォームでのビルド方法を記載します。
**ビルドに関しての問い合わせは受け付けておりません。うまくいかない場合は [GitHub Actions](https://github.com/shiguredo/sora-cpp-sdk-samples/blob/develop/.github/workflows/build.yml) の内容をご確認ください。**

### リポジトリをクローンする

[develop ブランチ](https://github.com/shiguredo/sora-cpp-sdk-samples.git) をクローンして利用してください。

```shell
$ git clone https://github.com/shiguredo/sora-cpp-sdk-samples.git
$ cd sora-cpp-sdk-samples
```

### Momo サンプルをビルドする

#### Windows x86_64 向けのビルドをする

##### 事前準備

以下のツールを準備してください。

- [Visual Studio 2019](https://visualstudio.microsoft.com/ja/downloads/)
  - C++ をビルドするためのコンポーネントを入れてください。
- Python 3.10.5

##### ビルド

```powershell
> python3 momo_sample\windows_x86_64\run.py
```

成功した場合、`_build\windows_x86_64\release\momo_sample\Release` に `momo_sample.exe` が作成されます。

```
\_BUILD\WINDOWS_X86_64\RELEASE\MOMO_SAMPLE\RELEASE
    momo_sample.exe
```

#### macOS arm64 向けのビルドをする

##### 事前準備

以下のツールを準備してください。

- Python 3.9.13

##### ビルド

```shell
$ python3 momo_sample/macos_arm64/run.py
```

成功した場合、`_build/macos_arm64/release/momo_sample` に `momo_sample` が作成されます。

```
_build/macos_arm64/release/momo_sample
└── momo_sample
```

#### Ubuntu 20.04 x86_64 向けのビルドをする

##### 事前準備

必要なパッケージをインストールしてください。

```shell
$ sudo apt install libdrm-dev
$ sudo apt install libva-dev
$ sudo apt install pkg-config
$ sudo apt install python3
```

##### ビルド

```shell
$ python3 momo_sample/ubuntu-20.04_x86_64/run.py
```

成功した場合、`_build/ubuntu-20.04_x86_64/release/momo_sample` に `momo_sample` が作成されます。

```
_build/ubuntu-20.04_x86_64/release/momo_sample/
└── momo_sample
```

#### Ubuntu 22.04 x86_64 向けのビルドをする

##### 事前準備

必要なパッケージをインストールしてください。

```shell
$ sudo apt install libdrm-dev
$ sudo apt install libva-dev
$ sudo apt install pkg-config
$ sudo apt install python3
```

##### ビルド

```shell
$ python3 momo_sample/ubuntu-22.04_x86_64/run.py
```

成功した場合、以下のファイルが作成されます。`_build/ubuntu-22.04_x86_64/release/momo_sample` に `momo_sample` が作成されます。

```
_build/ubuntu-22.04_x86_64/release/momo_sample/
└── momo_sample
```

#### Ubuntu 20.04 x86_64 で Ubuntu 20.04 armv8 Jetson 向けのビルドをする

**NVIDIA Jetson 上ではビルドできません。Ubuntu 20.04 x86_64 上でクロスコンパイルしたバイナリを利用するようにしてください。**

##### 事前準備

必要なパッケージをインストールしてください。

```shell
$ sudo apt install multistrap
$ sudo apt install binutils-aarch64-linux-gnu
$ sudo apt install python3
```

multistrap に insecure なリポジトリからの取得を許可する設定を行います。

```shell
$ sudo sed -e 's/Apt::Get::AllowUnauthenticated=true/Apt::Get::AllowUnauthenticated=true";\n$config_str .= " -o Acquire::AllowInsecureRepositories=true/' -i /usr/sbin/multistrap
```

##### ビルド

```shell
$ python3 momo_sample/ubuntu-20.04_armv8_jetson/run.py
```

成功した場合、以下のファイルが作成されます。`_build/ubuntu-20.04_armv8_jetson/release/momo_sample` に `momo_sample` が作成されます。

```
_build/ubuntu-20.04_armv8_jetson/release/momo_sample/
└── momo_sample
```

## 実行する

### コマンドラインから必要なオプションを指定して実行します

ビルドされたバイナリのあるディレクトリに移動して、コマンドラインから必要なオプションを指定して実行します。
以下は Sora サーバのシグナリング URL `wss://sora.example.com/signaling` の `sora` チャンネルに `sendrecv` で接続する例です。

Windows の場合
```powershell
> .\momo_sample.exe --signaling-url wss://sora.example.com/signaling --role sendrecv --channel-id sora --multistream true --use-sdl
```

Windows 以外の場合
```shell
$ ./momo_sample --signaling-url wss://sora.example.com/signaling --role sendrecv --channel-id sora --multistream true --use-sdl
```

#### Sora に関するオプション

設定内容については [Sora のドキュメント](https://sora-doc.shiguredo.jp/SIGNALING) も参考にしてください。

- `--signaling-url` : Sora サーバのシグナリング URL (必須)
- `--channel-id` : [channel_id](https://sora-doc.shiguredo.jp/SIGNALING#ee30e9) (必須)
- `--role` : [role](https://sora-doc.shiguredo.jp/SIGNALING#6d21b9) (必須)
    - sendrecv / sendonly / recvonly のいずれかを指定
- `--client-id` : [client_id](https://sora-doc.shiguredo.jp/SIGNALING#d00933) 
- `--video` : 映像の利用 (true/false)
    - 未指定の場合は true が設定されます
- `--audio` : 音声の利用 (true/false)
    - 未指定の場合は true が設定されます
- `--video-codec-type` : [ビデオコーデック指定](https://sora-doc.shiguredo.jp/SIGNALING#d47f4d)
    - VP8 / VP9 / AV1 / H264 が指定可能ですが利用可能なコーデックはプラットフォームに依存します
    - 未指定の場合は Sora のデフォルトである VP9 が利用されます
- `--audio-codec-type` : [オーディオコーデック指定](https://sora-doc.shiguredo.jp/SIGNALING#0fcf4e)
    - OPUS が指定可能です
    - 未指定の場合は Sora のデフォルトである OPUS が利用されます
- `--video-bit-rate` : [ビデオビットレート指定](https://sora-doc.shiguredo.jp/SIGNALING#5667cf)
    - 0 - 30000 の値が指定可能です
    - 0 は未指定と見なされます
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
    - 例) http://proxy.example.com:3128
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

#### その他のオプション

- `--help`
    - ヘルプを表示します
