# macOS でシステム CA を TLS 検証の信頼ストアに使う

- Priority: Medium
- Created: 2026-07-16
- Completed: {YYYY-MM-DD}
- Model: Composer
- Branch: feature/add-system-ca-store-macos
- Polished: {YYYY-MM-DD}

## 目的

macOS 上の WSS / TURN-TLS 証明書検証で、Keychain / Security.framework が管理するシステム（および必要ならユーザ）の信頼アンカーを使う。親 issue 0035 の macOS 実装。

## 優先度根拠

Medium。親 0035 と同じ。BoringSSL は Keychain を読まず、`/etc/ssl/cert.pem` がある環境ではファイル経由で一部の CA しか見えない。OS の信頼設定と一致しない。

## 現状

`SSLVerifier::VerifyX509` はプラットフォーム分岐なく、埋め込みルートと `X509_STORE_set_default_paths` に依存する。macOS の本来の信頼ストアは Keychain であり、現行実装はそれを使っていない。

## 設計方針

- 親 0035 の差し込み口を macOS 向けに実装する
- Security.framework でシステム信頼アンカーを取得し、PEM / DER を `X509_STORE` に載せる
- ユーザが Keychain に追加した社内 CA も、通常の TLS クライアントと同様に使えることを目指す（取得 API のスコープは実装時に Security のドキュメントで確定する）
- `ca_cert` 指定時はシステム CA を混ぜない

対象ビルドターゲット: `macos_arm64`

## 完了条件

- macOS で `ca_cert` 未指定時、システム CA のみで公開 CA の検証が通る
- Keychain に入れた独自 CA を信頼できること（取得スコープに含める場合）。含めない場合は issue に明示し、`ca_cert` で代替できることを完了条件に残す
- システム CA 読み込み失敗時は検証失敗 + 英語ログ
- 親 0035 の共通差し込み口を使っている

## 解決方法

1. Security.framework でアンカー証明書を列挙し OpenSSL/BoringSSL の `X509` に変換して `X509_STORE` へ追加する
2. リンク・ヘッダは既存の macOS ビルド（`run.py build macos_arm64`）に合わせる
3. sumomo 等で WSS / TURN-TLS の接続確認を行う

## 関連

- 親: `issues/0035-change-tls-trust-store-system-ca.md`
