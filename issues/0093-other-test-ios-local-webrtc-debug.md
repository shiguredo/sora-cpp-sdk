# test/ios をローカルビルドの webrtc や debug ビルドでも動作するようにする

- Created: 2026-09-11
- Completed: {YYYY-MM-DD}
- Branch: feature/fix-test-ios-local-webrtc-debug
- Polished: 2026-09-15

## 目的

`test/ios` の hello プロジェクトを、`--local-webrtc-build-dir` でローカルの webrtc-build を指定した場合と、`--debug` を指定した場合でもビルド・動作できるようにする。

## 現状

- `run.py build ios` は `install_dir = _install/ios/<configuration>` に SDK (libsora.a など) を配置する。`--local-webrtc-build-dir` を指定した場合、`buildbase.py` の `get_webrtc_info` はローカルの `_build/<platform>/<configuration>/webrtc` を参照し、webrtc を `_install/ios/<configuration>/webrtc` には配置しない
- `test/ios/hello.xcodeproj/project.pbxproj` は `../../_install/ios/release/` 配下のパスをハードコードしている
  - `libwebrtc.a`: `../../_install/ios/release/webrtc/lib/libwebrtc.a`
  - `libsora.a`: `../../_install/ios/release/sora/lib/libsora.a`
  - boost / blend2d も同様
  - `LIBRARY_SEARCH_PATHS` / `SYSTEM_HEADER_SEARCH_PATHS` / `CC` も `release` 固定
- このため、ローカルの webrtc-build を指定した場合は、Xcode プロジェクトが参照する `_install/ios/<configuration>/webrtc` に手動で配置する必要がある。なお `--package` で作成される tar.gz は `sora/` と `boost/` のみをアーカイブし、webrtc は含まないため、それで手動配置を代替することはできない
- `--debug` を指定した場合は SDK が `_install/ios/debug` に配置される。webrtc は `--local-webrtc-build-dir` 指定なしだとリリース版のアーカイブをダウンロードして `_install/ios/debug/webrtc` に配置される。Xcode プロジェクトのファイル参照 (libwebrtc.a / libsora.a / boost / blend2d) はすべて `../../_install/ios/release/...` を指しており、`_install/ios/debug` には存在しないためビルドに失敗する。include のパスも release を期待している
- ローカルの webrtc-build を指定した場合は、`run.py` の `install_deps` が `install_llvm` を `local_webrtc_build_dir is None` のときだけ呼ぶため、`_install/ios/<configuration>/llvm` が存在しない。`CC` と libcxx の `-isystem` の参照先も成立しない (ローカルビルドの clang / libcxx はそれぞれ `_source/<platform>/webrtc/src/third_party/llvm-build/Release+Asserts`、`_source/<platform>/webrtc/src/third_party/libc++/src` にある)
- `project.pbxproj` の libcxx の `-isystem` は構成で食い違っており、Debug 構成 (DDBC4BA8) は `_install/ios/release/llvm/libcxx/include`、Release 構成 (DDBC4BA9) は `_install/macos_arm64/release/llvm/libcxx/include` を参照している (iOS のプロジェクトが macOS のビルド成果物に依存しており、macos_arm64 がビルド済みの環境でしか動かない偶然に依存している)
- `run.py` の `if test:` 内の iOS ビルドはコメントアウトされており、`xcodebuild` は実行されない。iOS のテストは Xcode プロジェクトを手動でビルドして実機で確認している (過去の libwebrtc 更新時も実機での WSS 接続確認を行っている。x86_64 シミュレータ向けの libwebrtc.a は m120 以降存在せず、CI での iOS テストビルドは中止されている)
- 対応するソース: `test/ios/hello.xcodeproj/project.pbxproj`、`run.py`、`buildbase.py` の `build_webrtc` / `get_webrtc_info`

## 設計方針

- Xcode プロジェクトが参照する webrtc / boost / blend2d / sora / llvm (clang・libcxx) のパスを `release` 固定ではなくビルド構成に応じて切り替えられるようにする。xcconfig やユーザー定義ビルド設定を使い、`_install/ios/<configuration>/...` を参照する。macos_arm64 のビルド成果物への依存も解消する
- ローカルの webrtc-build を指定した場合に、Xcode プロジェクトが参照できる場所へ webrtc を配置する。`run.py` の `build_webrtc` / `get_webrtc_info` を見直してローカルの webrtc を `_install/ios/<configuration>/webrtc` に配置するか、Xcode プロジェクトがローカルのパスを直接参照できるようにする
  - 前者は `get_webrtc_info` が他プラットフォームや examples (sumomo / sdl_sample / messaging_recvonly_sample の各 run.py) でも使われる共有関数のため、iOS 限定の分岐またはコピー処理の追加が必要になる
  - 後者は Xcode プロジェクト側の設定に閉じるが、`--local-webrtc-build-dir` は任意のパスを指定できるため、そのパスを Xcode 側へ渡す仕組みが必要になる
- `run.py test` から iOS の `xcodebuild` を実行できるようにするか、手動ビルドの手順をドキュメント化するかを決める。現状の `if test:` のコメントアウトを整理する
- iOS シミュレータと実機のどちらを対象にするかを整理する (実機は signing が必要)。x86_64 シミュレータ向けの libwebrtc.a は m120 以降存在しないため、対象にする場合は arm64 シミュレータ用の libwebrtc をどう用意するかが前提になる

## 完了条件

- `--local-webrtc-build-dir` を指定してビルドした SDK で `test/ios` がビルド・動作すること
- `--debug` を指定してビルドした SDK で `test/ios` がビルド・動作すること
- 手動で webrtc を `_install/ios` に配置する手順が不要になっていること
- release ビルドで `run.py build ios` を通した成果物を Xcode プロジェクトでビルド・動作できること (既存手順に回帰がないこと)
- 対象デバイス (実機またはシミュレータ) と、Xcode プロジェクトの Debug / Release 構成と SDK の debug / release の対応が確定していること
