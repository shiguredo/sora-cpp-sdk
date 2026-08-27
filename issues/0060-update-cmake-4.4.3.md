# CMake を 4.4.3 にあげる

- Created: 2026-08-27
- Completed: {YYYY-MM-DD}
- Branch: feature/update-cmake-4.4.3
- Polished: {YYYY-MM-DD}

## 目的

CMake のバージョンを 4.4.2 から 4.4.3 に更新し、4.4 系の最新リリースに追従する。
v4.4.3 は 2026-08-25 にリリース済み。

## 現状

`DEPS` と `examples/DEPS` の `CMAKE_VERSION` で CMake のバージョンを管理している。現在は `4.4.2`。
この値は `run.py` のインストール処理、`test/android/app/build.gradle`、各 examples の `run.py` から参照されている。

## 設計方針

- `DEPS` と `examples/DEPS` の `CMAKE_VERSION` を `4.4.3` に更新する
- 互換性のない変更やビルド設定の追加は不要なはずだが、ビルド・テストで確認する

## 設計方針

- `DEPS` と `examples/DEPS` の `CMAKE_VERSION` を `4.4.3` に更新する
- 互換性のない変更やビルド設定の追加は不要なはずだが、ビルド・テストで確認する

## 懸念

- Android は `test/android/app/build.gradle` の `externalNativeBuild` が `CMAKE_VERSION` を読み込み、Gradle 経由で CMake のバージョン指定を行う。Android Gradle Plugin の対応範囲を超えると Android ビルドだけ失敗し得るため、他のプラットフォームとは別に Android の Gradle ビルドで確認する
- `buildbase.py` の `install_cmake` は GitHub Release のアセット名 `cmake-{version}-{platform}.{ext}` に依存している。v4.4.3 で各プラットフォームのアセットが公開されていることを確認する
- `examples/DEPS` の `CMAKE_VERSION` は各 examples の `run.py` 経由で参照されるため、メイン本体だけでなく examples 側のビルドも確認する

## 完了条件

- PR の CI が全プラットフォームで成功していること
  - CI のビルドジョブが全プラットフォームで成功していること
  - Android / iOS 以外のプラットフォームでは sumomo の E2E テスト (pytest) が成功していること
  - Android の Gradle ビルド（`test/android` のテストアプリ）が成功していること
- develop の `DEPS` と `examples/DEPS` の `CMAKE_VERSION` が `4.4.3` になっていること
- `CHANGES.md` に更新エントリを追記していること

### 変更履歴の書き方サンプル

`CHANGES.md` には以下のように追記する。担当者ハンドル `@<担当者>` は PR 作成者のものに書き換える。

```markdown
## develop

- [UPDATE] cmake のバージョンを 4.4.3 に上げる
  - @<担当者>

### misc

- [UPDATE] Examples の cmake のバージョンを 4.4.3 に上げる
  - @<担当者>
```

## 解決方法

- `DEPS` と `examples/DEPS` の `CMAKE_VERSION` を `4.4.3` に更新した
- `CHANGES.md` に更新内容を追記した
