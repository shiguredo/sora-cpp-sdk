# SDL サンプルを使ってみる

## 概要

[WebRTC SFU Sora](https://sora.shiguredo.jp/) と音声や映像の送受信を行い、[SDL (Simple DirectMedia Layer)](https://www.libsdl.org/) を利用して映像を表示するサンプルです。

このサンプルに [Sora Labo](https://sora-labo.shiguredo.app/) / [Tobi](https://tobi.shiguredo.jp/) に接続する機能を用意する予定は現在ありません。[Momo サンプルを使ってみる](./doc/USE_MOMO_SAMPLE.md) か独自に実装していただく必要があります。

- 参考記事 : [sora-cpp-sdk-samples にmomoのオプションを移植した](https://zenn.dev/tetsu_koba/articles/06e11dd4870796)

## 動作環境

[動作環境](../README.md#動作環境) をご確認ください。

接続先として WebRTC SFU Sora サーバ が必要です。[対応 Sora](../README.md#対応-sora) をご確認ください。

## サンプルをビルドする

以下にそれぞれのプラットフォームでのビルド方法を記載します。

**ビルドに関しての問い合わせは受け付けておりません。うまくいかない場合は [GitHub Actions](https://github.com/shiguredo/sora-cpp-sdk-samples/blob/develop/.github/workflows/build.yml) の内容をご確認ください。**


### リポジトリをクローンする

[develop ブランチ](https://github.com/shiguredo/sora-cpp-sdk-samples.git) をクローンして利用してください。

```shell
$ git clone https://github.com/shiguredo/sora-cpp-sdk-samples.git
$ cd sora-cpp-sdk-samples
```

### SDL サンプルをビルドする

#### Windows x86_64 向けのビルドをする

##### 事前準備

以下のツールを準備してください。

- [Visual Studio 2019](https://visualstudio.microsoft.com/ja/downloads/)
    - C++ をビルドするためのコンポーネントを入れてください。
- Python 3.10.5

##### ビルド

```powershell
> python3 sdl_sample\windows_x86_64\run.py
```

成功した場合、`_build\windows_x86_64\release\sdl_sample\Release` に `sdl_sample.exe` が作成されます。

```
\_BUILD\WINDOWS_X86_64\RELEASE\SDL_SAMPLE\RELEASE
    sdl_sample.exe
```

#### macOS arm64 向けのビルドをする

##### 事前準備

以下のツールを準備してください。

- Python 3.9.13

##### ビルド

```shell
$ python3 sdl_sample/macos_arm64/run.py
```

成功した場合、`_build/macos_arm64/release/sdl_sample` に `sdl_sample` が作成されます。

```
_build/macos_arm64/release/sdl_sample
└── sdl_sample
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
$ python3 sdl_sample/ubuntu-20.04_x86_64/run.py
```

成功した場合、`_build/ubuntu-20.04_x86_64/release/sdl_sample` に `sdl_sample` が作成されます。

```
_build/ubuntu-20.04_x86_64/release/sdl_sample/
└── sdl_sample
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
$ python3 sdl_sample/ubuntu-22.04_x86_64/run.py
```

成功した場合、以下のファイルが作成されます。`_build/ubuntu-22.04_x86_64/release/sdl_sample` に `sdl_sample` が作成されます。

```
_build/ubuntu-22.04_x86_64/release/sdl_sample/
└── sdl_sample
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
$ python3 sdl_sample/ubuntu-20.04_armv8_jetson/run.py
```

成功した場合、以下のファイルが作成されます。`_build/ubuntu-20.04_armv8_jetson/release/sdl_sample` に `sdl_sample` が作成されます。

```
_build/ubuntu-20.04_armv8_jetson/release/sdl_sample/
└── sdl_sample
```

## 実行する

### コマンドラインから必要なオプションを指定して実行します

ビルドされたバイナリのあるディレクトリに移動して、コマンドラインから必要なオプションを指定して実行します。
以下は Sora サーバのシグナリング URL `wss://sora.example.com/signaling` の `sora` チャンネルに `sendrecv` で接続する例です。

Windows の場合
```powershell
> .\sdl_sample.exe --signaling-url wss://sora.example.com/signaling --role sendrecv --channel-id sora --multistream true
```

Windows 以外の場合
```shell
$ ./sdl_sample --signaling-url wss://sora.example.com/signaling --role sendrecv --channel-id sora --multistream true
```

#### Sora に関するオプション

設定内容については [Sora のドキュメント](https://sora-doc.shiguredo.jp/SIGNALING) も参考にしてください。

- `--signaling-url` : Sora サーバのシグナリング URL (必須)
- `--channel-id` : channel_id (必須)
    - 任意のチャンネル ID
- `--role` : role (必須)
    - sendrecv / sendonly / recvonly のいずれかを指定
- `--video-codec-type` : ビデオコーデック指定
    - VP8 / VP9 / AV1 / H264 が指定可能ですが利用可能なコーデックはプラットフォームに依存します
    - 未指定の場合は Sora のデフォルトである VP9 が利用されます
- `--multistream` : マルチストリーム機能の利用 (true/false)
    - 未指定の場合は Sora の設定 (デフォルト: true) が設定されます

#### SDL に関するオプション

- `--width`
    - 映像を表示するウインドウの横幅を指定します
- `--height`
    - 映像を表示するウインドウの縦幅を指定します
- `--fullscreen`
    - 映像を表示するウインドウをフルスクリーンにします
- `--show-me`
    - 送信している自分の映像を表示します

#### その他のオプション

- `--help`
    - ヘルプを表示します
