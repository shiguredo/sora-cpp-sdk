# Android ビルドが compiler-rt builtins 不足で失敗するのを修正する

- Created: 2026-09-29
- Completed: 2026-09-29
- Branch: feature/update-libwebrtc-m155.8059.1.1
- Polished: {YYYY-MM-DD}

## 目的

libwebrtc を m155.8059.1.1 に上げた際、Android ビルドが Chromium clang の compiler-rt builtins 不足で失敗した。
原因と対処を記録し、次回以降の libwebrtc 更新で同じ調査を繰り返さないようにする。

## 現状

- Android ビルドは libwebrtc に合わせ、NDK の clang ではなく Chromium の clang を使う
  (`cmake/android.toolchain.cmake` が `ANDROID_OVERRIDE_C_COMPILER` / `ANDROID_OVERRIDE_CXX_COMPILER` で上書きする)
- m154.8037.1.2 の Chromium clang (llvmorg-24-init-3796-g20e97c4b) のパッケージには
  `lib/clang/24/lib/linux/libclang_rt.builtins-{aarch64,arm,i686,riscv64,x86_64}-android.a` が同梱されていた
- m155.8059.1.1 の Chromium clang (llvmorg-24-init-7747-g62397f8b) のパッケージには Android 向け builtins が同梱されなくなった
  (`lib/clang/24/lib/linux` ディレクトリ自体が無い)
  - 確認方法: 次の 2 つのパッケージの中身を比較した
    - `https://commondatastorage.googleapis.com/chromium-browser-clang/Linux_x64/clang-llvmorg-24-init-3796-g20e97c4b-2.tar.xz`
    - `https://commondatastorage.googleapis.com/chromium-browser-clang/Linux_x64/clang-llvmorg-24-init-7747-g62397f8b-27.tar.xz`
- clang ドライバ (LLVM 24 の `ToolChain::getCompilerRT`) は builtins を次の順で探す
  1. 新レイアウト: `<resource-dir>/lib/<triple>/libclang_rt.builtins.a`
  2. 旧レイアウト: `<resource-dir>/lib/linux/libclang_rt.builtins-<arch>-android.a`
  - どちらも無い場合は 1 のパスをリンカに渡すため、
    `ld.lld: error: cannot open .../lib/clang/24/lib/aarch64-none-linux-android29/libclang_rt.builtins.a` で失敗する
- 実際に CI の Android ジョブ (run 36533328407) で、
  blend2d の CMake コンパイラチェック (try-compile) のリンク時にこのエラーが発生した
- NDK r28b 側には `lib/clang/19/lib/linux/libclang_rt.builtins-*-android.a` が存在するが、
  Chromium clang のリソースディレクトリとは別の場所のため自動では使われない
- 影響: m155 の Android ビルドが成立しない

## 設計方針

- Chromium clang が旧レイアウトのパスも探すことを利用し、
  NDK が持つ `libclang_rt.builtins-*-android.a` を Chromium clang のリソースディレクトリ
  `<clang_dir>/lib/clang/<version>/lib/linux/` にコピーする
  - m154 の Chromium clang パッケージと同じ配置になり、ドライバの旧レイアウト探索で見つかる
- コピー元の NDK は Android ビルドで必ずインストールされる (`install_deps` が最初に処理する)
- 対象は NDK に存在する `libclang_rt.builtins-*-android.a` 全部とする
  (現在の `ANDROID_ABI` は arm64-v8a のみだが、m154 のパッケージと同様に全アーキテクチャ分を配置する)
- コピーには `copyfile_if_different` を使い、毎回のビルドで無駄な書き込みをしない

## 完了条件

- Android の CMake コンパイラチェックとリンクが成功すること
- 他プラットフォームのビルドと E2E に影響がないこと

## 解決方法

`run.py` に `install_android_clang_builtins()` を追加し、
`install_deps` の Android 分岐で `install_llvm` の後に呼ぶようにした。

- NDK 側: `<ndk>/toolchains/llvm/prebuilt/linux-x86_64/lib/clang/<ndk_clang_version>/lib/linux/` の
  `libclang_rt.builtins-*-android.a` をコピー元にする
  (`fix_clang_version` と `get_clang_version` で NDK clang のバージョンを解決する)
- Chromium clang 側: `<clang_dir>/lib/clang/<clang_version>/lib/linux/` を作成してコピーする
- あわせて `CHANGES.md` の `## develop` の m155 更新エントリに追記した

検証:

- CI run 36534975022 の Android ジョブが成功した (blend2d の configure、SDK ビルド、テスト、E2E、パッケージまで)
- 同じ run の全プラットフォームのビルドと E2E が成功した
- PR #401 として develop に squash merge された (`bb321ec7`、修正コミットは `e4713088`)
