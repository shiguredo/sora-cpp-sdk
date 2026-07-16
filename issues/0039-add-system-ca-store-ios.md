# iOS でシステム CA を TLS 検証の信頼ストアに使う

- Priority: Medium
- Created: 2026-07-16
- Completed: {YYYY-MM-DD}
- Model: Composer
- Branch: feature/add-system-ca-store-ios
- Polished: {YYYY-MM-DD}

## 目的

iOS 上の WSS / TURN-TLS 証明書検証で、Security.framework が管理するシステム信頼アンカーを使う。親 issue 0035 の iOS 実装。

## 優先度根拠

Medium。親 0035 と同じ。iOS でも Keychain / システム信頼を読まず埋め込みルート頼みになりやすい。モバイルでは独自 CA を MD プロファイル等で配布する運用があり、システム信頼との一致が重要。

## 現状

`SSLVerifier::VerifyX509` に iOS 向けのシステム CA 読み込みはない。サンプル（sumomo 等）は iOS 非対応だが、SDK 本体の `ios` ターゲットでは WSS / TURN-TLS 検証が動く。

## 設計方針

- 親 0035 の差し込み口を iOS 向けに実装する
- Security.framework でシステム信頼アンカーを取得し `X509_STORE` に載せる
- macOS（0037）と API が近い場合は共通化してよいが、iOS の制約（アプリサンドボックス、ユーザ信頼の扱い）を優先する
- `ca_cert` 指定時はシステム CA を混ぜない
- ホスト名検証の追加はこの issue の範囲外（親・現状どおりチェーン検証が主）

対象ビルドターゲット: `ios`

## 完了条件

- iOS で `ca_cert` 未指定時、システム信頼アンカーのみで公開 CA の検証が通る
- デバイス／シミュレータでシステム CA 読み込み失敗時は検証失敗 + 英語ログ
- `ca_cert` 指定時の契約が親 0035 どおりである
- 親 0035 の共通差し込み口を使っている

## 解決方法

1. Security.framework でアンカーを列挙し BoringSSL の `X509` に載せる
2. `python3 run.py build ios` でビルドが通ることを確認する
3. 実機またはシミュレータでシグナリング接続により検証する（プロジェクト内に iOS サンプルが無い場合は、検証手順を issue の解決方法追記で残す）

## 関連

- 親: `issues/0035-change-tls-trust-store-system-ca.md`
- 参考: `issues/0037-add-system-ca-store-macos.md`（Security.framework の類似実装）
