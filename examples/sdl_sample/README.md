# SDL サンプルを使ってみる

## 概要

[WebRTC SFU Sora](https://sora.shiguredo.jp/) と音声や映像の送受信を行い、[SDL (Simple DirectMedia Layer)](https://www.libsdl.org/) を利用して映像を表示するサンプルです。

## 動作環境

[動作環境](../../README.md#動作環境) をご確認ください。

接続先として WebRTC SFU Sora サーバ が必要です。[対応 Sora](../../README.md#対応-sora) をご確認ください。

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

### SDL サンプルをビルドする

#### Windows x86_64 向けのビルドをする

##### 事前準備

以下のツールを準備してください。

- [Visual Studio 2022](https://visualstudio.microsoft.com/ja/downloads/)
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

- Python 3.12.4

##### ビルド

```shell
python3 sdl_sample/macos_arm64/run.py
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
sudo apt install build-essential libxext-dev libx11-dev libgl-dev pkg-config python3
```

##### ビルド

```shell
python3 sdl_sample/ubuntu-20.04_x86_64/run.py
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
sudo apt install build-essential libxext-dev libx11-dev libgl-dev pkg-config python3
```

##### ビルド

```shell
python3 sdl_sample/ubuntu-22.04_x86_64/run.py
```

成功した場合、以下のファイルが作成されます。`_build/ubuntu-22.04_x86_64/release/sdl_sample` に `sdl_sample` が作成されます。

```
_build/ubuntu-22.04_x86_64/release/sdl_sample/
└── sdl_sample
```

#### Ubuntu 24.04 x86_64 向けのビルドをする

##### 事前準備

必要なパッケージをインストールしてください。

```shell
sudo apt install build-essential libxext-dev libx11-dev libgl-dev pkg-config python3
```

##### ビルド

```shell
python3 sdl_sample/ubuntu-24.04_x86_64/run.py
```

成功した場合、以下のファイルが作成されます。`_build/ubuntu-24.04_x86_64/release/sdl_sample` に `sdl_sample` が作成されます。

```
_build/ubuntu-24.04_x86_64/release/sdl_sample/
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
./sdl_sample --signaling-url wss://sora.example.com/signaling --role sendrecv --channel-id sora --multistream true
```

#### 必須オプション

- `--signaling-url` : Sora サーバのシグナリング URL (必須)
- `--channel-id` : [channel_id](https://sora-doc.shiguredo.jp/SIGNALING#ee30e9) (必須)
- `--role` : [role](https://sora-doc.shiguredo.jp/SIGNALING#6d21b9) (必須)
  - sendrecv / sendonly / recvonly のいずれかを指定

#### SDL サンプル実行に関するオプション

- `--log-level` : 実行時にターミナルに出力するログのレベル
  - `verbose->0,info->1,warning->2,error->3,none->4` の値が指定可能です

#### Sora に関するオプション

設定内容については [Sora のドキュメント](https://sora-doc.shiguredo.jp/SIGNALING) も参考にしてください。

- `--video-codec-type` : [ビデオコーデック指定](https://sora-doc.shiguredo.jp/SIGNALING#d47f4d)
  - VP8 / VP9 / AV1 / H264 / H265 が指定可能ですが利用可能なコーデックはプラットフォームに依存します
  - 未指定の場合は Sora のデフォルトである VP9 が利用されます
- `--multistream` : [マルチストリーム](https://sora-doc.shiguredo.jp/SIGNALING#808bc2) 機能の利用 (true/false)
  - 未指定の場合は Sora の設定 (デフォルト: true) が設定されます
- `--video` : 映像の利用 (true/false)
  - 未指定の場合は true が設定されます
- `--audio` : 音声の利用 (true/false)
  - 未指定の場合は true が設定されます
- `--metadata` : [メタデータ](https://sora-doc.shiguredo.jp/SIGNALING#414142)
  - JSON 形式の文字列を指定してください

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
