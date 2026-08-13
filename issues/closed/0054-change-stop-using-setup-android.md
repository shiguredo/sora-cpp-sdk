# GitHub Actions から android-actions/setup-android を利用しないようにする

- Created: 2026-08-13
- Completed: 2026-08-13
- Branch: feature/change-stop-using-setup-android
- Polished: 2026-08-13

## 目的

CI / Release ワークフローの Android ジョブでサードパーティ Action `android-actions/setup-android` を利用しないようにする。

setup-android が行う処理（`ANDROID_SDK_ROOT` の export（既存値の維持）、cmdline-tools / platform-tools のインストール、ライセンス許諾、PATH 追加）は GitHub-hosted runner にプリインストールされた Android SDK で代替できる。また setup-android は platform / build-tools を提供しておらず、削除の前後でビルドへの供給元は変わらない。

## 現状

- `.github/workflows/ci.yml` と `.github/workflows/release.yml` の Android 向けステップで `android-actions/setup-android` を呼んでいる
- 直前に `actions/setup-java` で JDK 17 を入れている（デフォルト JDK 11 回避のため）
- Android ジョブの `python3 run.py build --test --run-e2e-test --package android` は、本体ライブラリのビルドに加えて `android/Sora` の `assembleRelease`（AAR 生成、AGP 8.2.0、compileSdk 34）と `test/android` の `assemble`（AGP 8.10.0、compileSdk 34）を実行する
- GitHub-hosted runner（ubuntu-22.04）には Android SDK がプリインストールされており、環境変数 `ANDROID_SDK_ROOT=/usr/local/lib/android/sdk` がデフォルトで設定されている
- `run.py` の Android ターゲット処理では、環境変数 `ANDROID_SDK_ROOT` が設定済みでそのパスが存在する場合は Commandline Tools を入れず、未設定または設定済みでもパスが存在しないときだけ `install_android_sdk_cmdline_tools`（`buildbase.py`）を実行して `ANDROID_SDK_ROOT` を設定する
- Commandline Tools の版は `DEPS` の `ANDROID_SDK_CMDLINE_TOOLS_VERSION`（現状 `13114758`）で管理している。ただしこれは run.py の fallback 経路専用で、CI の cmdline-tools は setup-android のデフォルト（`14742923`）が使われていた
- setup-android が設定していた problem matcher（ビルドエラーの annotation 表示）は削除で失われるが、ビルド成否には影響しない

## 設計方針

- 削除後の Android SDK の用意方法の選択肢と推奨:

| 選択肢 | 内容 | 評価 |
|---|---|---|
| A: runner のプリインストール SDK を利用 | setup-android を削除するのみで、`run.py` / `buildbase.py` / `DEPS` は変更しない | 推奨。ビルドの成否は解決方法 2 の確認項目で検証する |
| B: `run.py` の `install_android_sdk_cmdline_tools` を主経路にする | `ANDROID_SDK_ROOT` 未設定時に cmdline-tools をインストールする既存の fallback を主経路へ格上げする | 現状の `install_android_sdk_cmdline_tools` は cmdline-tools とライセンス許諾のみで platform / build-tools を入れないため、gradle ビルドが AGP の自動ダウンロードに依存することになる。また CI では毎ジョブ cmdline-tools を再ダウンロードすることになる。拡張が必要になりスコープが広がる |
| C: SDK 一式を削除 | setup-android / JDK / `ANDROID_SDK_CMDLINE_TOOLS_VERSION` をすべて削除する | 不可。JDK 17 の削除で AGP 8.x の gradle ビルドが実行不能になるため。またローカルビルドの fallback 経路も失う |

- 選択肢 A を基本とし、確認の結果 CI の Android ジョブが失敗した場合は対応方針を検討する。選択肢 B の拡張が必要になった場合は本 issue のスコープを広げず別 issue として切り出す
- JDK 17 ステップは gradle ビルド（AGP 8.x）が JDK 17 を要求するため、削除しない
- 関連 issue の 0012（Android の targetSdk 35 引き上げ、open）で compileSdk が変更される場合は、runner のプリインストール SDK への依存を再確認する
- ubuntu-22.04 イメージは 2026-09-17 に非推奨開始、2027-04-17 にサポート終了予定のため、runner 移行時（ubuntu-24.04 等）にも Android ジョブがプリインストール SDK で動くことを再確認する
- 決定: 選択肢 A（runner のプリインストール SDK を利用）
- 理由: CI の Android ジョブでビルドが成功し、run.py の分岐が従来どおりスキップされた（fallback が実行されていない）ことを確認した。platform / build-tools の供給元は削除の前後で変わらない。選択肢 B は `install_android_sdk_cmdline_tools` が platform / build-tools を入れず拡張が必要になるため不採用とし、選択肢 C は AGP 8.x の gradle ビルドに JDK 17 が必須のため不可とした

## 完了条件

- `.github/workflows/ci.yml` と `.github/workflows/release.yml` に `android-actions/setup-android` の参照が残っていない
- Android SDK の用意方法の方針（選択肢 A / B / C のいずれか）を決め、解決方法 2 の確認結果とあわせて決定内容と理由を「設計方針」セクション末尾に「決定:」「理由:」の形式で追記して記録している
- 実装ブランチで ci.yml の Android ジョブが通っている。選択肢 B の拡張を別 issue に切り出した場合は、失敗原因と対応方針の記録をもって本条件に代える
  - release.yml はタグプッシュ（`202*`）でのみ発火するため直接検証できない。両ファイルの Android ジョブが同じ構成であることを確認し、ci.yml の成功で代替する
  - ci.yml は CHANGES.md のみの変更では発火しないため、検証は CHANGES.md 以外の変更を含むコミットのプッシュで済ませる
- `CHANGES.md` の `## develop` 配下の `### misc` に、サブ箇条書きと担当者行（`- @<担当者>`）を含む `[CHANGE]` エントリとして、setup-android 利用をやめた旨の記述がある

## 解決方法

- `.github/workflows/ci.yml` と `.github/workflows/release.yml` から `Setup Android SDK`（`android-actions/setup-android`）ステップを削除した
- `run.py` / `buildbase.py` / `DEPS` は変更していない（選択肢 A）
- `actions/setup-java`（JDK 17）ステップは変更していない
- `CHANGES.md` の `## develop` 配下の `### misc` に `[CHANGE]` エントリを追加した

### 確認結果

- CI の Android ジョブ（`Build sora-cpp-sdk for android`）が成功した
- ジョブログに `commandlinetools-linux-13114758` のダウンロード行と `sdkmanager` の実行行が現れず、run.py の fallback（`install_android_sdk_cmdline_tools`）が実行されていないことを確認した（`android-ndk-r28b` のダウンロード行は NDK インストールによるもので判断対象外）
- gradle ビルド（`assembleRelease` / `assemble`）が `BUILD SUCCESSFUL` で完了し、runner のプリインストール SDK（platform 34 と AGP が要求する build-tools）で通ることを確認した
- `dl.google.com` からのダウンロードは `android-ndk-r28b` のみで、AGP の自動ダウンロードは発生していない（CMake 4.3.2 は run.py の `install_cmake` が GitHub Releases から取得した）
- 成果物（`sora-cpp-sdk-..._android.tar.gz`）のアップロードが成功したことを確認した
- release.yml は ci.yml と同じ変更（同一ステップの削除）を反映しているため、ci.yml の成功で代替する
