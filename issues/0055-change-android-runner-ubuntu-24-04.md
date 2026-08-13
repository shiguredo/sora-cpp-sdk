# GitHub Actions の Android ジョブのランナーを ubuntu-24.04 に変更する

- Created: 2026-08-13
- Completed: {YYYY-MM-DD}
- Branch: feature/change-android-runner-ubuntu-24-04
- Polished: {YYYY-MM-DD}

## 目的

CI / Release ワークフローの Android ジョブが利用している ubuntu-22.04 runner イメージは、2026-09-17 に非推奨開始、2027-04-17 にサポート終了予定のため（actions/runner-images issue #14254）、ubuntu-24.04 に移行する。

## 現状

- `.github/workflows/ci.yml` と `.github/workflows/release.yml` の Android ジョブの `runs-on` が `ubuntu-22.04`
- ubuntu-22.04 のデフォルト JDK は 11 のため、`actions/setup-java` で JDK 17 を明示指定している（コメント「JDK を指定しないとデフォルトの JDK 11 で動作するため指定する」）
- ubuntu-24.04 のデフォルト JDK は 17 で、Android SDK がプリインストールされており、環境変数 `ANDROID_SDK_ROOT=/usr/local/lib/android/sdk` がデフォルトで設定されている
- 関連 issue の 0054（`android-actions/setup-android` の削除、open）が先に実装されている場合も、ubuntu-24.04 のプリインストール SDK への依存は維持される

## 設計方針

- Android ジョブの `runs-on` を `ubuntu-24.04` に変更するのみとし、SDK の用意方法は変更しない
- `actions/setup-java` の JDK 17 明示指定は、ubuntu-24.04 ではデフォルト JDK が 17 になるため、明示指定を残す場合は陳腐化したコメントを更新する。明示指定の要否も確認して決定する
- 本 issue の対象は Android ジョブのみとする。同じ `ubuntu-22.04` を使う他ジョブ（ubuntu-22.04_x86_64 / ubuntu-22.04_armv8）は対象外とする

## 完了条件

- `.github/workflows/ci.yml` と `.github/workflows/release.yml` の Android ジョブの `runs-on` が `ubuntu-24.04` になっている
- 実装ブランチで ci.yml の Android ジョブが通っている。release.yml はタグプッシュ（`202*`）でのみ発火するため直接検証できないが、両ファイルの Android ジョブが同じ構成であることを確認し、ci.yml の成功で代替する。なお ci.yml は CHANGES.md のみの変更では発火しないため、検証は CHANGES.md 以外の変更を含むコミットのプッシュで済ませる
- `CHANGES.md` の `## develop` 配下の `### misc` に、サブ箇条書きと担当者行（`- @<担当者>`）を含む `[CHANGE]` エントリとして、Android ジョブのランナーを ubuntu-24.04 に変更した旨の記述がある

## 解決方法

1. `ci.yml` / `release.yml` の Android ジョブの `runs-on` を `ubuntu-22.04` から `ubuntu-24.04` に変更する
2. Android ジョブが ubuntu-24.04 でビルドに成功し、成果物（`Sora-release.aar` 等）が生成されることを確認する。確認項目:
   - 環境変数 `ANDROID_SDK_ROOT=/usr/local/lib/android/sdk` が ubuntu-24.04 でもデフォルトで設定されており、`run.py` の分岐（`install_android_sdk_cmdline_tools`）が従来どおりスキップされること
   - compileSdk 34 の platform / build-tools が ubuntu-24.04 のプリインストール SDK に含まれており、gradle ビルド（`assembleRelease` / `assemble`）が通ること
   - デフォルト JDK が 17 に変わることの影響を確認し、`actions/setup-java` の明示指定とコメントを適切に更新すること
3. `CHANGES.md` を更新する
