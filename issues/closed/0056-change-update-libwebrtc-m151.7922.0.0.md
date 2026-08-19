# libwebrtc を m151.7922.0.0 にあげる

- Created: 2026-08-17
- Completed: 2026-08-19
- Branch: feature/update-m151.7922.0.0
- Polished: 2026-08-17

## 目的

libwebrtc のバージョンを m150.7871.3.1 から m151.7922.0.0 に更新し、m151 系の最新リリースに追従する。
m151.7922.0.0 は webrtc-build 側で対応が完了しリリース済みのため、sora-cpp-sdk も追従する。

## 現状

m151 への更新実装は `feature/update-m151.7922.0.0` ブランチで完了し、origin に push 済みだが、develop へのマージは未実施である。
iOS / Android で動作検証を実施したあと、PR を作成する。

なお、`run.py build --test --run-e2e-test` は iOS / Android ではテスト実行を伴わない（iOS は no-op、Android は `test/android` の Gradle プロジェクトのビルドのみ）。iOS / Android の動作検証は実機での手動確認が主体になる。

### rebase 時のコンフリクト解決 (2026-08-19)

develop から `feature/update-m151.7922.0.0` への rebase で、以下のコンフリクトを解決した。

- `DEPS` / `examples/DEPS`:
  - `WEBRTC_BUILD_VERSION` のみ `m151.7922.0.0` を採用した
  - それ以外（`BOOST_VERSION` / `CMAKE_VERSION` / `SORA_CPP_SDK_VERSION` / `CLI11_VERSION`）は rebase 先の develop 側（`BOOST_VERSION=1.92.0` / `CMAKE_VERSION=4.4.2` / `SORA_CPP_SDK_VERSION=2026.2.1` / `CLI11_VERSION=v2.7.2`）を採用した
- `CHANGES.md`:
  - develop 側でリリース済みの `## 2026.2.1` セクションを維持した
  - feature 側の m151 更新エントリを `## develop` セクションに配置した

### 実機動作確認 (2026-08-19)

- iOS: `test/ios` の hello を実機で起動し、WSS 接続できることを確認した
- Android: `test/android` の hello を実機で起動し、WSS 接続できることを確認した

## 設計方針

`feature/update-m151.7922.0.0` ブランチで実施済みの変更は以下。

- `DEPS` と `examples/DEPS` の `WEBRTC_BUILD_VERSION` を `m151.7922.0.0` に更新する
- libwebrtc の JNI エクスポート形式が従来の `Java_org_webrtc_` から JNI Zero の `Java_J_N_` 形式に変わったため、`run.py` の `install_deps` 関数で Android ビルド時に保持する JNI シンボルの一覧に `Java_J_N_` を追加する
- `CHANGES.md` に更新内容を追記する

## 完了条件

- PR の CI が全プラットフォームで成功していること
  - CI のビルドジョブが全プラットフォームで成功していること
  - Android / iOS 以外のプラットフォームでは sumomo の E2E テスト (pytest) が成功していること
- iOS / Android で動作検証を実施し、問題がないことを確認していること
  - iOS は `test/ios` の Xcode プロジェクトのビルド・起動、Android は `test/android` の Gradle プロジェクトのビルド・起動を確認する
- `feature/update-m151.7922.0.0` の内容が develop にマージされていること
- develop の `DEPS` と `examples/DEPS` の `WEBRTC_BUILD_VERSION` が `m151.7922.0.0` になっていること
- `run.py` の `install_deps` 関数の JNI シンボル保持対象に `Java_J_N_` が含まれていること
- `CHANGES.md` の `## develop` に `[UPDATE] libwebrtc のバージョンを m151.7922.0.0 に上げる`、`### misc` に `[UPDATE] Examples の WEBRTC_BUILD_VERSION を m151.7922.0.0 にあげる` のエントリが追加されていること

### 変更履歴の書き方サンプル

`CHANGES.md` には以下のように追記する。担当者ハンドル `@<担当者>` は PR 作成者のものに書き換える。

```markdown
## develop

- [UPDATE] libwebrtc のバージョンを m151.7922.0.0 に上げる
  - Android のテストで WebRTC の JNI Zero シンボルを保持する
    - libwebrtc の JNI エクスポート形式が従来の `Java_org_webrtc_` から JNI Zero の `Java_J_N_` 形式へ変わったため、run.py のシンボル保持対象に `Java_J_N_` を追加する
  - @<担当者>

### misc

- [UPDATE] Examples の WEBRTC_BUILD_VERSION を m151.7922.0.0 にあげる
  - @<担当者>
```

## 解決方法

- `DEPS` と `examples/DEPS` の `WEBRTC_BUILD_VERSION` を `m151.7922.0.0` に更新した
- `run.py` の `install_deps` 関数の JNI シンボル保持対象に `Java_J_N_` を追加した
- `CHANGES.md` に更新内容を追記した
- iOS / Android で `test/ios` / `test/android` の hello を実機で起動し、WSS 接続できることを確認した
