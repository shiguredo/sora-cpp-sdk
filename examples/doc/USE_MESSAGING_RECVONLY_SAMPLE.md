# メッセージング受信サンプルを使ってみる

## 概要

[WebRTC SFU Sora](https://sora.shiguredo.jp/) の [メッセージング機能](https://sora-doc.shiguredo.jp/MESSAGING) を使って送信されたメッセージを受信して、標準出力にメッセージのラベルとデータサイズを表示するサンプルです。

## 動作環境

[動作環境](../../README.md#動作環境) をご確認ください。

接続先として WebRTC SFU Sora サーバ が必要です。[対応 Sora](../../README.md#対応-sora) をご確認ください。

## サンプルをビルドする

以下にそれぞれのプラットフォームでのビルド方法を記載します。

**ビルドに関しての問い合わせは受け付けておりません。うまくいかない場合は [GitHub Actions](https://github.com/shiguredo/sora-cpp-sdk/blob/develop/.github/workflows/build.yml) の内容をご確認ください。**


### リポジトリをクローンする

[sora-cpp-sdk](https://github.com/shiguredo/sora-cpp-sdk) をクローンして、examples 以下のディレクトリを利用してください。

```shell
$ git clone https://github.com/shiguredo/sora-cpp-sdk.git
$ cd sora-cpp-sdk/examples
```

### メッセージング受信サンプルをビルドする

#### Windows x86_64 向けのビルドをする

##### 事前準備

以下のツールを準備してください。

- [Visual Studio 2019](https://visualstudio.microsoft.com/ja/downloads/)
    - C++ をビルドするためのコンポーネントを入れてください。
- Python 3.10.5

##### ビルド

```powershell
> python3 messaging_recvonly_sample\windows_x86_64\run.py
```

成功した場合、`_build\windows_x86_64\release\messaging_recvonly_sample\Release` に `messaging_recvonly_sample.exe` が作成されます。

```
\_BUILD\WINDOWS_X86_64\RELEASE\MESSAGING_RECVONLY_SAMPLE\RELEASE
    messaging_recvonly_sample.exe
```

#### macOS arm64 向けのビルドをする

##### 事前準備

以下のツールを準備してください。

- Python 3.9.13

##### ビルド

```shell
$ python3 messaging_recvonly_sample/macos_arm64/run.py
```

成功した場合、`_build/macos_arm64/release/messaging_recvonly_sample` に `messaging_recvonly_sample` が作成されます。

```
_build/macos_arm64/release/messaging_recvonly_sample
└── messaging_recvonly_sample
```

#### Ubuntu 20.04 x86_64 向けのビルドをする

##### 事前準備

必要なパッケージをインストールしてください。

```shell
$ sudo apt install libx11-dev
$ sudo apt install libdrm-dev
$ sudo apt install libva-dev
$ sudo apt install pkg-config
$ sudo apt install python3
```

##### ビルド

```shell
$ python3 messaging_recvonly_sample/ubuntu-20.04_x86_64/run.py
```

成功した場合、`_build/ubuntu-20.04_x86_64/release/messaging_recvonly_sample` に `messaging_recvonly_sample` が作成されます。

```
_build/ubuntu-20.04_x86_64/release/messaging_recvonly_sample/
└── messaging_recvonly_sample
```

#### Ubuntu 22.04 x86_64 向けのビルドをする

##### 事前準備

必要なパッケージをインストールしてください。

```shell
$ sudo apt install libx11-dev
$ sudo apt install libdrm-dev
$ sudo apt install libva-dev
$ sudo apt install pkg-config
$ sudo apt install python3
```

##### ビルド

```shell
$ python3 messaging_recvonly_sample/ubuntu-22.04_x86_64/run.py
```

成功した場合、以下のファイルが作成されます。`_build/ubuntu-22.04_x86_64/release/messaging_recvonly_sample` に `messaging_recvonly_sample` が作成されます。

```
_build/ubuntu-22.04_x86_64/release/messaging_recvonly_sample/
└── messaging_recvonly_sample
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
$ python3 messaging_recvonly_sample/ubuntu-20.04_armv8_jetson/run.py
```

成功した場合、以下のファイルが作成されます。`_build/ubuntu-20.04_armv8_jetson/release/messaging_recvonly_sample` に `messaging_recvonly_sample` が作成されます。

```
_build/ubuntu-20.04_armv8_jetson/release/messaging_recvonly_sample/
└── messaging_recvonly_sample
```

## 実行する

### コマンドラインから必要なオプションを指定して実行します

ビルドされたバイナリのあるディレクトリに移動して、コマンドラインから必要なオプションを指定して実行します。
以下は Sora サーバのシグナリング URL `wss://sora.example.com/signaling` の `sora` チャンネルに接続する例です。
デフォルトでは `#sora-devtools` ラベルのメッセージが受信対象となります。

Windows の場合
```powershell
> .\messaging_recvonly_sample.exe --signaling-url wss://sora.example.com/signaling --channel-id sora
```

Windows 以外の場合
```shell
$ ./messaging_recvonly_sample --signaling-url wss://sora.example.com/signaling --channel-id sora
```

#### Sora に関するオプション

設定内容については [Sora のドキュメント](https://sora-doc.shiguredo.jp/SIGNALING) も参考にしてください。

- `--signaling-url` : Sora サーバのシグナリング URL (必須)
- `--channel-id` : channel_id (必須)
    - 任意のチャンネル ID
- `--data-channels` : 受信対象のデータチャネルのリストを JSON 形式で指定します
    - 未指定の場合は `[{"label":"#sora-devtools", "direction":"recvonly"}]` が設定されます
    - 指定可能な内容については Sora のドキュメントの ["type": "connect" 時の "data_channels"](https://sora-doc.shiguredo.jp/MESSAGING#8e04a8) を参照してください

#### その他のオプション

- `--help`
    - ヘルプを表示します
