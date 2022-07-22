# SDL サンプルを使ってみる

## 概要

[Sora](https://sora.shiguredo.jp/) と映像の送受信を行い、[SDL (Simple DirectMedia Layer)](https://www.libsdl.org/) を利用して映像を表示するサンプルです。

このサンプルに [Sora Labo](https://sora-labo.shiguredo.app/) / [Tobi](https://tobi.shiguredo.jp/) に接続する機能を用意する予定は現在ありません。独自に実装していただく必要があります。

  - 参考記事 : [sora-cpp-sdk-samples にmomoのオプションを移植した](https://zenn.dev/tetsu_koba/articles/06e11dd4870796)

## 動作環境

[動作環境](../README.md#動作環境) をご確認ください。

接続先として Sora サーバ が必要です。[対応 Sora](../README.md#対応-sora) をご確認ください。

## サンプルをビルドする

**ビルド方法の詳細は [GitHub Actions](https://github.com/shiguredo/sora-cpp-sdk-samples/blob/develop/.github/workflows/build.yml) をご確認ください、ビルドに関しての問い合わせは受け付けておりません**


### リポジトリをクローンする

[main ブランチ](https://github.com/shiguredo/sora-cpp-sdk-samples/tree/main) をクローンして利用してください。
develop ブランチは開発ブランチであり、正常に動作しないことがあります。

```shell
$ git clone -b main https://github.com/shiguredo/sora-cpp-sdk-samples.git
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

<details>
<summary>tree _build\windows_x86_64\release\sdl_sample\Release /F</summary>
\_BUILD\WINDOWS_X86_64\RELEASE\SDL_SAMPLE\RELEASE
    sdl_sample.exe
    sdl_sample.exp
    sdl_sample.lib
</details>

#### macOS arm64 向けのビルドをする

##### 事前準備

以下のツールを準備してください。

- Python 3.9.13

##### ビルド

```shell
$ python3 sdl_sample/macos_arm64/run.py
```

成功した場合、`_build/macos_arm64/release/sdl_sample` に `sdl_sample` が作成されます。

<details>
<summary>tree _build/macos_arm64/release/sdl_sample</summary>
_build/macos_arm64/release/sdl_sample
├── CMakeCache.txt
├── CMakeFiles
│   ├── 3.23.1
│   │   ├── CMakeCCompiler.cmake
│   │   ├── CMakeCXXCompiler.cmake
│   │   ├── CMakeDetermineCompilerABI_C.bin
│   │   ├── CMakeDetermineCompilerABI_CXX.bin
│   │   ├── CMakeSystem.cmake
│   │   ├── CompilerIdC
│   │   │   ├── CMakeCCompilerId.c
│   │   │   ├── CMakeCCompilerId.o
│   │   │   └── tmp
│   │   └── CompilerIdCXX
│   │       ├── CMakeCXXCompilerId.cpp
│   │       ├── CMakeCXXCompilerId.o
│   │       └── tmp
│   ├── CMakeDirectoryInformation.cmake
│   ├── CMakeOutput.log
│   ├── CMakeTmp
│   ├── Makefile.cmake
│   ├── Makefile2
│   ├── TargetDirectories.txt
│   ├── cmake.check_cache
│   ├── progress.marks
│   └── sdl_sample.dir
│       ├── DependInfo.cmake
│       ├── Users
│       │   └── mio
│       │       └── Git
│       │           └── shiguredo
│       │               └── sora-cpp-sdk-samples
│       │                   └── sdl_sample
│       │                       └── src
│       │                           ├── sdl_renderer.cpp.o
│       │                           ├── sdl_renderer.cpp.o.d
│       │                           ├── sdl_sample.cpp.o
│       │                           └── sdl_sample.cpp.o.d
│       ├── build.make
│       ├── cmake_clean.cmake
│       ├── compiler_depend.internal
│       ├── compiler_depend.make
│       ├── compiler_depend.ts
│       ├── depend.make
│       ├── flags.make
│       ├── link.txt
│       └── progress.make
├── Makefile
├── cmake_install.cmake
└── sdl_sample
</details>

#### Ubuntu 20.04 x86_64 向けのビルドをする

##### 事前準備

以下のパッケージをインストールしてください。

- libdrm-dev
- pkg-config
- python3 

```shell
$ sudo apt install libdrm-dev
$ sudo apt install pkg-config
$ sudo apt install python3
```

##### ビルド

```shell
$ python3 sdl_sample/ubuntu-20.04_x86_64/run.py
```

成功した場合、`_build/ubuntu-20.04_x86_64/release/sdl_sample` に `sdl_sample` が作成されます。

<details>
<summary>tree _build/macos_arm64/release/sdl_sample</summary>
_build/ubuntu-20.04_x86_64/release/sdl_sample/
├── CMakeCache.txt
├── CMakeFiles
│   ├── 3.23.1
│   │   ├── CMakeCCompiler.cmake
│   │   ├── CMakeCXXCompiler.cmake
│   │   ├── CMakeDetermineCompilerABI_C.bin
│   │   ├── CMakeDetermineCompilerABI_CXX.bin
│   │   ├── CMakeSystem.cmake
│   │   ├── CompilerIdC
│   │   │   ├── CMakeCCompilerId.c
│   │   │   ├── a.out
│   │   │   └── tmp
│   │   └── CompilerIdCXX
│   │       ├── CMakeCXXCompilerId.cpp
│   │       ├── a.out
│   │       └── tmp
│   ├── CMakeDirectoryInformation.cmake
│   ├── CMakeError.log
│   ├── CMakeOutput.log
│   ├── CMakeTmp
│   ├── Makefile.cmake
│   ├── Makefile2
│   ├── TargetDirectories.txt
│   ├── cmake.check_cache
│   ├── progress.marks
│   └── sdl_sample.dir
│       ├── DependInfo.cmake
│       ├── build.make
│       ├── cmake_clean.cmake
│       ├── compiler_depend.make
│       ├── compiler_depend.ts
│       ├── depend.make
│       ├── flags.make
│       ├── home
│       │   └── mio
│       │       └── git
│       │           └── sora-cpp-sdk-samples_x86
│       │               └── sdl_sample
│       │                   └── src
│       │                       ├── sdl_renderer.cpp.o
│       │                       ├── sdl_renderer.cpp.o.d
│       │                       ├── sdl_sample.cpp.o
│       │                       └── sdl_sample.cpp.o.d
│       ├── link.txt
│       └── progress.make
├── Makefile
├── cmake_install.cmake
└── sdl_sample
</details>

#### Ubuntu 22.04 x86_64 向けのビルドをする

##### 事前準備

以下のパッケージをインストールしてください。

- libdrm-dev
- pkg-config
- python3 

```shell
$ sudo apt install libdrm-dev
$ sudo apt install pkg-config
$ sudo apt install python3
```

##### ビルド

```shell
$ python3 sdl_sample/ubuntu-22.04_x86_64/run.py
```

成功した場合、以下のファイルが作成されます。`_build/ubuntu-22.04_x86_64/release/sdl_sample` に `sdl_sample` が作成されます。

```shell
$ ls -l _build/ubuntu-22.04_x86_64/release/sdl_sample/sdl_sample
```

#### Ubuntu 20.04 x86_64 で Ubuntu 20.04 armv8 Jetson 向けのビルドをする

**NVIDIA Jetson 上ではビルドできません。Ubuntu 20.04 x86_64 上でクロスコンパイルしたバイナリを利用するようにしてください。**

##### 事前準備

以下のパッケージをインストールしてください。

- multistrap
  - insecure なリポジトリからの取得を許可する設定を行います。[GitHub Actions](https://github.com/shiguredo/sora-cpp-sdk-samples/blob/develop/.github/workflows/build.yml) をご確認ください
- binutils-aarch64-linux-gnu
- python3

```shell
$ sudo apt install multistrap
$ sudo apt install binutils-aarch64-linux-gnu
$ sudo apt install python3
$ sudo sed -e 's/Apt::Get::AllowUnauthenticated=true/Apt::Get::AllowUnauthenticated=true";\n$config_str .= " -o Acquire::AllowInsecureRepositories=true/' -i /usr/sbin/multistrap
```

##### ビルド

```shell
$ python3 sdl_sample/ubuntu-20.04_x86_64/run.py
```

成功した場合、以下のファイルが作成されます。`_build/ubuntu-20.04_armv8_jetson/release/sdl_sample` に `sdl_sample` が作成されます。

```shell
$ ls -l _build/ubuntu-20.04_armv8_jetson/release/sdl_sample/sdl_sample
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
    -  sendrecv / sendonly / recvonly のいずれかを指定
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
