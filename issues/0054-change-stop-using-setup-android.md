# GitHub Actions から android-actions/setup-android を利用しないようにする

- Created: 2026-08-13
- Completed: {YYYY-MM-DD}
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

## 完了条件

- `.github/workflows/ci.yml` と `.github/workflows/release.yml` に `android-actions/setup-android` の参照が残っていない
- Android SDK の用意方法の方針（選択肢 A / B / C のいずれか）を決め、解決方法 2 の確認結果とあわせて決定内容と理由を「設計方針」セクション末尾に「決定:」「理由:」の形式で追記して記録している
- 実装ブランチで ci.yml の Android ジョブが通っている。選択肢 B の拡張を別 issue に切り出した場合は、失敗原因と対応方針の記録をもって本条件に代える
  - release.yml はタグプッシュ（`202*`）でのみ発火するため直接検証できない。両ファイルの Android ジョブが同じ構成であることを確認し、ci.yml の成功で代替する
  - ci.yml は CHANGES.md のみの変更では発火しないため、検証は CHANGES.md 以外の変更を含むコミットのプッシュで済ませる
- `CHANGES.md` の `## develop` 配下の `### misc` に、サブ箇条書きと担当者行（`- @<担当者>`）を含む `[CHANGE]` エントリとして、setup-android 利用をやめた旨の記述がある

## 解決方法

1. `ci.yml` / `release.yml` から `Setup Android SDK`（`android-actions/setup-android`）ステップを削除する
2. 削除後、Android ジョブのビルドが成功し、成果物（`Sora-release.aar` 等）が従来どおり生成されることを確認する。確認項目:
   - runner のデフォルト環境変数 `ANDROID_SDK_ROOT` により、従来どおりスキップされること。スキップはログに現れないため、ビルドログに `commandlinetools-linux-13114758` のダウンロード行と、`sdkmanager` と `--licenses` を同一行に含む実行行（`yes | ... sdkmanager --sdk_root=... --licenses` の形）が現れないことで確認する（`android-ndk-r28b` のダウンロード行は NDK インストールによるもので判断対象外）。確認は GitHub Actions のステップログ検索、または `gh run view --log --branch feature/change-stop-using-setup-android` の出力の grep で行う。現れた場合は runner の前提が崩れているため、原因を特定して記録する
   - gradle ビルド（`assembleRelease` / `assemble`）が runner のプリインストール SDK（platform 34 と、AGP 8.2.0 / 8.10.0 が要求する build-tools（いずれもプリインストール済み）、ライセンス許諾済み）で通ること。gradle は `ANDROID_SDK_ROOT` / `ANDROID_HOME` の環境変数で SDK を解決する（`android/Sora` と `test/android` に local.properties は存在しない）。なお `test/android` の `externalNativeBuild` が要求する CMake 4.3.2（DEPS の `CMAKE_VERSION`）はプリインストール SDK に含まれないため、AGP の自動ダウンロードが発生する想定である。自動ダウンロード（CMake、build-tools 等）の発生は gradle ログの `Downloading https://dl.google.com/...` 行で判定し、その事実を完了条件 2 の「決定:」「理由:」の追記時に含めて記録する
   - なお、Android ジョブでは E2E テストは実行されないため、検証はビルド成功のみで行う
