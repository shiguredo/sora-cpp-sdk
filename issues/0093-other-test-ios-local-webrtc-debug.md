# test/ios をローカルビルドの webrtc や debug ビルドでも動作するようにする

- Created: 2026-09-11
- Completed: {YYYY-MM-DD}
- Branch: feature/fix-test-ios-local-webrtc-debug
- Polished: {YYYY-MM-DD}

## 目的

`test/ios` の hello プロジェクトを、`--local-webrtc-build-dir` でローカルの webrtc-build を指定した場合と、`--debug` を指定した場合でもビルド・動作できるようにする。

## 現状

- `run.py build ios` は `install_dir = _install/ios/<configuration>` に SDK (libsora.a など) を配置する。`--local-webrtc-build-dir` を指定した場合、`buildbase.py` の `get_webrtc_info` はローカルの `_build/<platform>/<configuration>/webrtc` を参照し、webrtc を `_install/ios/<configuration>/webrtc` には配置しない
- `test/ios/hello.xcodeproj/project.pbxproj` は `../../_install/ios/release/` 配下のパスをハードコードしている
  - `libwebrtc.a`: `../../_install/ios/release/webrtc/lib/libwebrtc.a`
  - `libsora.a`: `../../_install/ios/release/sora/lib/libsora.a`
  - boost / blend2d も同様
  - `LIBRARY_SEARCH_PATHS` / `SYSTEM_HEADER_SEARCH_PATHS` / `CC` も `release` 固定
- このため、ローカルの webrtc-build を指定した場合は動作時に `_install/ios/<configuration>` の下に webrtc を手動で配置する必要がある (ビルド時に `--package` で作成した tar.gz を展開して配置すれば解決できる)
- `--debug` を指定した場合は webrtc や SDK が `_install/ios/debug` に配置されるが、Xcode プロジェクトは `release` を参照するためビルドに失敗する。`libsora.a` を `Release-iphoneos` で探すためパスが見つからず、include のパスも release を期待している
- `run.py` の `if test:` 内の iOS ビルドはコメントアウトされており、`xcodebuild` は実行されない。iOS のテストは Xcode プロジェクトを手動でビルドして実機で確認している
- 対応するソース: `test/ios/hello.xcodeproj/project.pbxproj`、`run.py`、`buildbase.py` の `build_webrtc` / `get_webrtc_info`

## 設計方針

- Xcode プロジェクトが参照する webrtc / boost / blend2d / sora のパスを `release` 固定ではなくビルド構成に応じて切り替えられるようにする。`xcconfig` やユーザー定義ビルド設定を使い、`_install/ios/<configuration>/...` を参照する
- ローカルの webrtc-build を指定した場合に、Xcode プロジェクトが参照できる場所へ webrtc を配置する。`run.py` の `build_webrtc` / `get_webrtc_info` を見直してローカルの webrtc を `_install/ios/<configuration>/webrtc` に配置するか、Xcode プロジェクトがローカルのパスを直接参照できるようにする
- `run.py test` から iOS の `xcodebuild` を実行できるようにするか、手動ビルドの手順をドキュメント化するかを決める。現状の `if test:` のコメントアウトを整理する
- iOS シミュレータと実機のどちらを対象にするかを整理する (実機は signing が必要)

## 完了条件

- `--local-webrtc-build-dir` を指定してビルドした SDK で `test/ios` が動作すること
- `--debug` を指定してビルドした SDK で `test/ios` がビルド・動作すること
- 手動で webrtc を `_install/ios` に配置する手順が不要になっていること
- release ビルドの既存手順に回帰がないこと
