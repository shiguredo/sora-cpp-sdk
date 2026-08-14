# GitHub Actions の Android ジョブのランナーを ubuntu-24.04 に変更する

- Created: 2026-08-13
- Completed: 2026-08-13
- Branch: feature/change-android-runner-ubuntu-24-04
- Polished: 2026-08-13

## 目的

CI / Release ワークフローの Android ジョブが利用している ubuntu-22.04 runner イメージは、2026-09-17 に非推奨開始、2027-04-17 にサポート終了予定のため（actions/runner-images issue #14254）、ubuntu-24.04 に移行する。

## 現状

- `.github/workflows/ci.yml` と `.github/workflows/release.yml` の Android ジョブの `runs-on` が `ubuntu-22.04`
- ubuntu-22.04 のデフォルト JDK は 11 のため、`actions/setup-java` で JDK 17 を明示指定している（コメント「JDK を指定しないとデフォルトの JDK 11 で動作するため指定する」）
- 0054（`android-actions/setup-android` の削除）は実装済みで、Android SDK は runner のプリインストール SDK（環境変数 `ANDROID_SDK_ROOT=/usr/local/lib/android/sdk`）に依存している

## 移行先の前提（ubuntu-24.04）

- デフォルト JDK は 17
- Android SDK がプリインストールされており、環境変数 `ANDROID_SDK_ROOT`（および `ANDROID_HOME`）がデフォルトで設定されている
- プリインストール SDK には platform android-34、build-tools 34.0.0〜37.0.0、cmdline-tools 12.0 が含まれる。platform android-34 と build-tools 34.0.0〜37.0.0 は 22.04 と同一の構成である（actions/runner-images の Ubuntu2204 / Ubuntu2404-Readme で確認）
- cmdline-tools のバージョンは 22.04（9.0）から 12.0 に変わるが、CI では使用されない（`ANDROID_SDK_CMDLINE_TOOLS_VERSION` は `ANDROID_SDK_ROOT` が未設定、または設定済みでもそのパスが存在しない環境向けの fallback 専用）

## 設計方針

- Android ジョブの `runs-on` を `ubuntu-24.04` に変更するのみとし、SDK の用意方法は変更しない
- ubuntu-26.04 ではなく 24.04 を選ぶ理由: 26.04 の runner イメージは public preview であり（actions/runner-images issue #14226）、Android SDK の構成が検証済みの 24.04 へまず移行する。26.04 への移行は別 issue で扱う
- 0054 で決定済みのとおり `actions/setup-java` の JDK 17 明示指定は削除しない。ubuntu-24.04 ではデフォルト JDK が 17 になるため、デフォルト JDK 11 前提のコメントを「AGP 8.x の gradle ビルドが要求する JDK 17 を、ランナーイメージのデフォルトに依存せず明示指定する」趣旨の内容に更新する
- 本 issue の対象は Android ジョブのみとする。同じ `ubuntu-22.04` を使う他ジョブ（build-ubuntu / e2e-test の `ubuntu-22.04_x86_64` / `ubuntu-22.04_armv8` エントリと、build-ubuntu-examples の `ubuntu-22.04_x86_64` エントリ）は Android SDK に依存せず本 issue の検証対象に含める必要がないため、ランナー移行は本 issue の範囲外として別 issue で扱う
- 関連 issue の 0012（Android の targetSdk 35 引き上げ、open）で compileSdk が変更される場合は、ubuntu-24.04 のプリインストール SDK（platform android-34）への依存を再確認する
- 確認の結果ビルドが失敗した場合は原因を調査し、SDK の用意方法の変更が必要な場合は本 issue のスコープを広げず別 issue として切り出す

## 完了条件

- `.github/workflows/ci.yml` と `.github/workflows/release.yml` の build-ubuntu ジョブの matrix エントリ `android` の `runs-on` が `ubuntu-24.04` になっている
- 解決方法 2 の確認項目をすべて実施し、確認結果を「解決方法」セクションに記録している
- ci.yml / release.yml の両ファイルの `Setup JDK 17` ステップのコメントが、設計方針に記載した趣旨の内容に更新されている（明示指定は維持）
- 実装ブランチで ci.yml の Android ジョブが通っている。release.yml はタグプッシュ（`202*`）でのみ発火するため直接検証できないが、両ファイルの Android ジョブが同じ構成であることを確認し、ci.yml の成功で代替する。なお ci.yml は CHANGES.md のみの変更では発火しないため、検証は CHANGES.md 以外の変更を含むコミットのプッシュで済ませる
- `CHANGES.md` の `## develop` 配下の `### misc` に、サブ箇条書きと担当者行（`- @<担当者>`）を含む `[CHANGE]` エントリとして、Android ジョブのランナーを ubuntu-24.04 に変更した旨の記述がある

## 解決方法

- `ci.yml` / `release.yml` の build-ubuntu ジョブの matrix エントリ `android` の `runs-on` を `ubuntu-22.04` から `ubuntu-24.04` に変更した
- `Setup JDK 17` ステップのコメントを「AGP 8.x の Gradle ビルドが要求する JDK 17 を、ランナーイメージのデフォルトに依存せず明示指定する」に更新した（明示指定は維持）
- `CHANGES.md` の `## develop` 配下の `### misc` に `[CHANGE]` エントリを追加した

### 確認結果

- CI の Android ジョブ（`Build sora-cpp-sdk for android`）が ubuntu-24.04 で成功した
- ジョブログに `commandlinetools-linux-13114758` のダウンロード行と `sdkmanager` の実行行が現れず、`run.py` の fallback（`install_android_sdk_cmdline_tools`）がスキップされたことを確認した（`android-ndk-r28b` のダウンロード行は NDK インストールによるもので判断対象外）
- gradle ビルド（`assembleRelease` / `assemble`）が `BUILD SUCCESSFUL` で完了し、ubuntu-24.04 のプリインストール SDK（platform android-34 と、AGP 8.2.0 / 8.10.0 が要求する build-tools）で通ることを確認した
- `dl.google.com/android/repository` からのダウンロードは `android-ndk-r28b` のみで、AGP による SDK コンポーネントの自動インストールが発生しなかった
- `actions/setup-java` が JDK 17（Temurin 17.0.19-10）を明示指定しており、デフォルト JDK の変化（11→17）はビルドに影響しないことを確認した
- 成果物（`sora-cpp-sdk-2026.2.0-canary.28_android.tar.gz`）の生成とアップロードが成功したことを確認した
- release.yml は ci.yml と同じ変更を反映しているため、ci.yml の成功で代替する
