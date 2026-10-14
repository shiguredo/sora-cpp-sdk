# Windows のビルドを MSVC から libwebrtc と同じ clang-cl + libc++ に変更する

- Created: 2026-10-14
- Completed: {YYYY-MM-DD}
- Branch: feature/change-windows-clang-libcxx-build
- Polished: {YYYY-MM-DD}

## 目的

Windows の Sora C++ SDK のビルドを、MSVC (cl.exe) + MSVC STL から libwebrtc と同じ Clang (clang-cl) + libc++ に変更する。MSVC でビルドした libsora を MSVC STL でコンパイルしたアプリケーションから利用する構成では、libwebrtc が利用する libc++ とは標準ライブラリが異なるため、libwebrtc の型を跨いだ受け渡しで ABI 互換性を保証できない。libwebrtc のビルドに使われたのと同じコンパイラと標準ライブラリを使うことで、libwebrtc と標準ライブラリを揃える。

## 現状

### Windows のビルド構成

Windows では MSVC (cl.exe) と MSVC STL でビルドしている。CMake のジェネレータは Visual Studio ジェネレータで、ビルド生成物は `_build/windows_x86_64/release/<target>/Release/<target>.exe` のように構成ごとのサブディレクトリに出力される。

`CMakeLists.txt` には `USE_LIBCXX` オプションがあるが、Windows 向けの設定はなく、libc++ を指定する経路が `-nostdinc++` と `-isystem` の GCC 互換の形式でしか用意されていない。

`buildbase.py` には Ninja と clang-cl を扱う処理がなく、`run.py` は Windows で `CMAKE_MSVC_RUNTIME_LIBRARY` を指定していない。

### clang-cl をそのまま指定できない理由

Windows の libwebrtc には `clang.exe` / `clang++.exe` が含まれず `clang-cl.exe` のみが含まれる。clang-cl は MSVC (cl.exe) 互換のコマンドラインを解釈するドライバであり、`-nostdinc++` や `-isystem` のような GCC 互換のオプションはそのままでは解釈されない。

### ビルドツールと環境変数

CMake のジェネレータに Ninja を使う場合、Ninja 本体を用意する必要がある。また clang-cl は MSVC 互換ドライバであってもコンパイラとして Visual Studio を利用しないが、MSVC のヘッダー、Windows SDK、`rc.exe` / `mt.exe`、静的ライブラリを作成する `llvm-lib.exe` は Visual Studio から取得する必要がある。

### CUDA (NVCODEC)

Windows では CUDA のソースを `cuda_compile` (nvcc) でコンパイルしている。ただし Windows の CUDA のソースにはデバイスコードが含まれていない。

### 依存ライブラリ

Boost、Catch2、VPL、Blend2D、SDL3 は MSVC でビルドしている。このうち Boost は標準ライブラリを利用するため、libsora と Boost で標準ライブラリが異なると ABI が食い違う。

## 設計方針

- コンパイラには libwebrtc が利用するのと同じリビジョンの `clang-cl.exe` を指定する。`install_llvm` が導入するものを使い、別途 Clang をインストールする必要はなくする
- 標準ライブラリは libc++ にする。libc++ の名前空間と ABI バージョンは libwebrtc が利用する `__config_site` から与えられるため、`_LIBCPP_ABI_NAMESPACE` や `_LIBCPP_ABI_VERSION` は自前で定義しない
- clang-cl に GCC 互換のオプションを渡す場合は `/clang:` を経由する。libc++ の指定は `/clang:-nostdinc++` と `/I<libc++ のインクルードディレクトリ>` になる
- CRT は libwebrtc に合わせて静的リンク (`/MT`) にする。`CMAKE_MSVC_RUNTIME_LIBRARY` で指定し、第三者ライブラリの CMake プロジェクトにも効かせるために CMP0091 を NEW にする
- 例外処理 (`/EHsc`) は clang-cl では `/EHsc` を指定しないと無効なため明示的に有効にする
- CMake のジェネレータは Ninja にする。Visual Studio ジェネレータはツールセットを自前で決めるため `CMAKE_CXX_COMPILER` で指定したコンパイラを利用しない
- リンカは CMake が clang-cl に合わせて選択するため明示しない。`CMAKE_AR` は Visual Studio の LLVM に含まれる `llvm-lib.exe` が解決される
- Ninja と `vswhere.exe` は `_install/<platform>/<config>` 以下に自動でダウンロードする
- Visual Studio の環境変数は `VsDevCmd.bat` から取り込む。`-arch` と `-host_arch` で対象とホストのアーキテクチャを明示する
- CUDA (NVCODEC) は維持する。Windows の CUDA のソースはデバイスコードを含まないため、nvcc ではなく clang-cl で通常の C++ ソースとしてコンパイルし、CUDA のインクルードディレクトリと `cudart_static.lib` の取得に `find_package(CUDA)` を利用する
- 依存ライブラリも clang-cl + libc++ でビルドする。Boost は ABI を揃えるため libc++ 版を利用し、Boost.Build の toolset に `clang-win` を指定する
- 利用者が Windows でアプリケーションをビルドする場合も clang-cl + libc++ が必要になるため、変更履歴とドキュメントに記載する

## 完了条件

- Windows x86_64 で `python3 run.py build windows_x86_64 --test` が通り、テストが通る
- `--debug` / `--relwithdebinfo` でもビルドとテストが通る
- `--disable-cuda` を付けない場合もビルドが通る (CUDA のソースが clang-cl でコンパイルされる)
- `_install/windows_x86_64/<config>/sora` にインストールされる libsora と、テスト・サンプルが同じ libc++ でビルドされる
- サンプル (`sumomo` / `sdl_sample` / `messaging_recvonly_sample`) が Windows でビルドできる
- macOS、Ubuntu、Raspberry Pi OS、iOS、Android のビルド結果が変わらない

## 解決方法

`buildbase.py` を変更する。

- `get_vsdevcmd_arch` を追加し、`x86_64` を `x64`、`arm64` を `arm64` に変換する
- `setup_visual_studio_env` で `VsDevCmd.bat` に `-arch` と `-host_arch` を渡し、出力から環境変数を取り込む
- `get_windows_clang_cmake_args` を追加し、`CMAKE_C_COMPILER` / `CMAKE_CXX_COMPILER` に `clang-cl.exe`、`CMAKE_MAKE_PROGRAM` に `ninja.exe` を指定してジェネレータに Ninja を使う
- `install_windows_build_tools` を追加し、Ninja と `vswhere.exe` の導入と Visual Studio の環境変数の取り込みを行う
- `install_vpl` と `install_sdl3` に `CMAKE_MSVC_RUNTIME_LIBRARY` と `-DCMAKE_POLICY_DEFAULT_CMP0091=NEW` を追加する
- `install_amf` で AMF の `AMFSTL.h` が MSVC STL のヘッダーを取り込まないようにする
- `_replace_dir` を追加し、展開直後のディレクトリの移動をリトライする

`run.py` を変更する。

- `get_common_cmake_args` の Windows 分岐で `CMAKE_MSVC_RUNTIME_LIBRARY` と `-DCMAKE_POLICY_DEFAULT_CMP0091=NEW` を追加する
- sora 本体、テスト、依存ライブラリ (Boost、VPL、Blend2D、Catch2、SDL3、AMF) の Windows のビルドで `get_windows_clang_cmake_args`、`USE_LIBCXX`、`LIBCXX_INCLUDE_DIR` を指定する
- Boost の Windows のビルドで toolset に `clang-win`、`cxx` に `clang-cl.exe`、`cflags` に `/MT` (`/MTd`)、`cxxflags` に `/EHsc` と libc++ の指定を追加する
- Windows で `install_windows_build_tools` を呼び、テストのビルドと実行ファイルのパスを Ninja の出力構成に合わせる

`CMakeLists.txt` を変更する。

- Windows で `USE_LIBCXX` が有効な場合に `/clang:-nostdinc++` と `/I${LIBCXX_INCLUDE_DIR}` を指定する
- `MSVC_RUNTIME_LIBRARY` をターゲットのプロパティに指定する
- Windows の CUDA のソースを `cuda_compile` ではなく clang-cl でコンパイルする
- libc++ が参照する `ntdll.dll` と `userenv.dll` の API のシンボルを解決するために `ntdll.lib` と `userenv.lib` をリンクする
- Windows のコンパイル定義から `_ITERATOR_DEBUG_LEVEL` を削除し、`_LIBCPP_HARDENING_MODE=_LIBCPP_HARDENING_MODE_EXTENSIVE` と `_LIBCPP_DISABLE_VISIBILITY_ANNOTATIONS` を追加する

`DEPS` に `NINJA_VERSION` と `VSWHERE_VERSION` を追加する。

`test/CMakeLists.txt` と `examples/*/CMakeLists.txt` に Windows の libc++ と静的 CRT の指定を追加し、`examples/*/run.py` で SDL3 のビルドに clang-cl と Ninja を利用するようにする。

`.vscode` の C++ の設定と、GitHub Actions のワークフローを Ninja の出力構成に合わせる。
