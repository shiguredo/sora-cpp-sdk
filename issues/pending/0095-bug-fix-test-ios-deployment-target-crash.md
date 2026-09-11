# test/ios で IPHONEOS_DEPLOYMENT_TARGET を 18.4 にするとクラッシュする

- Created: 2026-09-11
- Completed: {YYYY-MM-DD}
- Branch: feature/fix-test-ios-deployment-target-crash
- Polished: {YYYY-MM-DD}

## 目的

test/ios の Xcode プロジェクト `hello.xcodeproj` で `IPHONEOS_DEPLOYMENT_TARGET` を 18.4 にしたときに、デプロイ直後にクラッシュする事象を調査し、修正する。

## 現状

- `IPHONEOS_DEPLOYMENT_TARGET` を 15.4 から 18.4 に変更すると、デプロイ後に即クラッシュする
- クラッシュせず起動できても Sora に接続できず、そのままになることがある
- 15.4 のままでは再現しない
- Xcode のバージョン変更によって起動時の動作に影響が出るようになった可能性がある
- `src/device_list.cpp` の `SORA_CPP_SDK_IOS` の分岐が有効にならず `RecordingIsAvailable` / `PlayoutIsAvailable` が呼ばれている可能性が指摘されている (ビルド時に `SORA_CPP_SDK_TARGET` が ios でない、またはビルドキャッシュの残留など)
- 現状 test/ios でのみ再現し、Sora Unity SDK では再現していない
- 対応 PR: https://github.com/shiguredo/sora-cpp-sdk/pull/324

## 設計方針

- `IPHONEOS_DEPLOYMENT_TARGET` を 18.4 にしたときのクラッシュログとスタックトレースを取得し、クラッシュ箇所を特定する
- `src/device_list.cpp` の `SORA_CPP_SDK_IOS` の分岐が有効になっているか、ビルド時の `SORA_CPP_SDK_TARGET` とビルドキャッシュを確認する
- Xcode のバージョンと `IPHONEOS_DEPLOYMENT_TARGET` の組み合わせによる影響を確認する
- test/ios 以外で再現しない理由を切り分ける

## 完了条件

- `IPHONEOS_DEPLOYMENT_TARGET` を 18.4 にしてもクラッシュしないこと
- Sora に接続できること
- 原因と判断根拠が issue に記録されていること

## Pending 理由

- 現状 test/ios でのみ再現し、Sora Unity SDK では再現しない
- `IPHONEOS_DEPLOYMENT_TARGET` を書き換えることは通常のアップデートタイミング以外ではなく、発生条件が限定的
- Xcode のバージョンに依存する可能性があり、原因の特定に時間がかかる

## Pending 解除条件

- クラッシュの原因が特定され、修正方針が決まったこと
