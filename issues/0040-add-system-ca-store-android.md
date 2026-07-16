# Android でシステム CA を TLS 検証の信頼ストアに使う

- Priority: Medium
- Created: 2026-07-16
- Completed: {YYYY-MM-DD}
- Model: Composer
- Branch: feature/add-system-ca-store-android
- Polished: {YYYY-MM-DD}

## 目的

Android 上の WSS / TURN-TLS 証明書検証で、端末のシステム CA を信頼アンカーとして使う。親 issue 0035 の Android 実装。

## 優先度根拠

Medium。親 0035 と同じ。Android のシステム CA は `/system/etc/security/cacerts` や APEX 配下などにあり、BoringSSL 既定の `/etc/ssl/...` とは一致しない。現行は埋め込みルート頼みになりやすい。

## 現状

`SSLVerifier::VerifyX509` に Android 向けのシステム CA 読み込みはない。NDK / C++ 側から Java の `TrustManager` を直接使う経路も現状の検証コードにはない。

## 設計方針

- 親 0035 の差し込み口を Android 向けに実装する
- システム CA の取得方法は実装時に比較して選ぶ。候補の例:
  - システム CA ディレクトリ（従来の `cacerts` や現行 Android の配置）から証明書を読んで `X509_STORE` に載せる
  - JNI 経由でプラットフォームの信頼アンカーを取得する
- ユーザ CA / アプリ Network Security Config をどこまで反映するかは実装時に決め、範囲外なら `ca_cert` で代替すると明記する
- `ca_cert` 指定時はシステム CA を混ぜない

対象ビルドターゲット: `android`

## 完了条件

- Android で `ca_cert` 未指定時、システム CA のみで公開 CA の検証が通る
- システム CA 読み込み失敗時は検証失敗 + 英語ログ
- `ca_cert` 指定時の契約が親 0035 どおりである
- 親 0035 の共通差し込み口を使っている
- `python3 run.py build android` が通る

## 解決方法

1. 上記候補から Android バージョン差に耐える方式を選び、システム CA を `X509_STORE` へ載せる
2. 必要なら JNI ヘルパを最小限追加する（依存は増やしすぎない）
3. 実機またはエミュレータでシグナリング接続により検証する（サンプルが無い場合は検証手順を解決方法に残す）

## 関連

- 親: `issues/0035-change-tls-trust-store-system-ca.md`
