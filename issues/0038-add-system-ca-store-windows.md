# Windows でシステム CA を TLS 検証の信頼ストアに使う

- Priority: Medium
- Created: 2026-07-16
- Completed: {YYYY-MM-DD}
- Model: Composer
- Branch: feature/add-system-ca-store-windows
- Polished: {YYYY-MM-DD}

## 目的

Windows 上の WSS / TURN-TLS 証明書検証で、Windows 証明書ストアの信頼アンカーを使う。親 issue 0035 の Windows 実装。

## 優先度根拠

Medium。親 0035 と同じ。Windows には `/etc/ssl/...` がなく、現行は埋め込みルート頼みになりやすい。企業環境の社内 CA は証明書ストア経由で配られることが多く、システム CA 対応の効果が大きい。

## 現状

`SSLVerifier::VerifyX509` は Windows 証明書ストア（CryptoAPI / CNG）を参照しない。`X509_STORE_set_default_paths` も Windows では実質無効になりやすい。

## 設計方針

- 親 0035 の差し込み口を Windows 向けに実装する
- 少なくとも「信頼されたルート証明機関」（ROOT）ストアからアンカーを読み、`X509_STORE` に載せる
- 中間証明機関ストアをどこまで見るかは実装時に決める（ルート検証に中間が必要なケースの有無を確認する）
- `ca_cert` 指定時はシステム CA を混ぜない

対象ビルドターゲット: `windows_x86_64`

## 完了条件

- Windows で `ca_cert` 未指定時、証明書ストアのルート CA のみで公開 CA の検証が通る
- 証明書ストアに入れた独自ルート CA を信頼できる
- システム CA 読み込み失敗時は検証失敗 + 英語ログ
- 親 0035 の共通差し込み口を使っている

## 解決方法

1. `CertOpenSystemStore` 等で ROOT ストアを開き、証明書を BoringSSL の `X509` に変換して `X509_STORE` へ追加する
2. 必要な Windows ライブラリのリンクを CMake / ビルドスクリプトに追加する
3. sumomo 等で WSS / TURN-TLS の接続確認を行う

## 関連

- 親: `issues/0035-change-tls-trust-store-system-ca.md`
