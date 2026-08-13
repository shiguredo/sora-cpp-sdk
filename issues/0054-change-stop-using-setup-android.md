# GitHub Actions から android-actions/setup-android を利用しないようにする

- Created: 2026-08-13
- Completed: {YYYY-MM-DD}
- Branch: feature/change-stop-using-setup-android
- Polished: {YYYY-MM-DD}

## 目的

CI / Release ワークフローの Android ジョブでサードパーティ Action `android-actions/setup-android` を利用しないようにする。外部 Action への依存をやめ、バージョン追従の手間を減らす。

## 現状

- `.github/workflows/ci.yml` と `.github/workflows/release.yml` の Android 向けステップで `android-actions/setup-android`（コミット固定、コメント `v4.0.1`）を呼んでいる
- 直前に `actions/setup-java` で JDK 17 を入れている（コメント上、デフォルト JDK 11 回避のため。setup-android 時代の組み合わせ要件に由来）
- `run.py` の Android ターゲット処理では、`ANDROID_SDK_ROOT` が既に存在する場合は Commandline Tools を入れず、未設定のときだけ `install_android_sdk_cmdline_tools`（`buildbase.py`）を実行して `ANDROID_SDK_ROOT` を設定する
- Commandline Tools の版は `DEPS` の `ANDROID_SDK_CMDLINE_TOOLS_VERSION`（現状 `13114758`）で管理している

## 設計方針

- 本 issue の主目的は `android-actions/setup-android` の削除である
- 削除後に Android SDK をどう用意するか（`run.py` の `install_android_sdk_cmdline_tools` に任せるか、別手段にするか、JDK ステップをどうするか等）は、削除の影響を見たうえで改めて検討する
- 削除後の代替手段を本 issue の時点で決め打ちしない

## 完了条件

- `.github/workflows/ci.yml` と `.github/workflows/release.yml` に `android-actions/setup-android` の参照が残っていない
- 削除後の Android SDK 用意方法について方針を決め、それに従って CI の Android ビルドが通る状態になっている
- `CHANGES.md` の `## develop` に、setup-android 利用をやめた旨のエントリがある

## 解決方法

1. `ci.yml` / `release.yml` から `Setup Android SDK`（`android-actions/setup-android`）ステップを削除する
2. 削除後、Android ジョブがどう壊れるかを確認する
3. その結果を踏まえて、Android SDK の用意方法と `actions/setup-java`（JDK 17）の扱いを再検討し、必要な対応を入れる
4. `CHANGES.md` を更新する
