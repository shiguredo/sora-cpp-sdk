# Linux でシステム CA を TLS 検証の信頼ストアに使う

- Priority: Medium
- Created: 2026-07-16
- Completed: {YYYY-MM-DD}
- Model: Composer
- Branch: feature/add-system-ca-store-linux
- Polished: {YYYY-MM-DD}

## 目的

Linux 上の WSS / TURN-TLS 証明書検証で、OS が提供するシステム CA（`ca-certificates` 等）を信頼アンカーとして使う。親 issue 0035 の Linux 実装。

## 優先度根拠

Medium。親 0035 と同じ。Linux は `/etc/ssl/certs` 等が存在する環境が多く、現行の `X509_STORE_set_default_paths` でも動く場合があるが、方針として「埋め込みルートではなくシステム CA」を明示する必要がある。

## 現状

`SSLVerifier::VerifyX509`（`src/ssl_verifier.cpp`）は `ca_cert` 未指定時に Let's Encrypt ハードコード、WebRTC `ssl_roots.h`、`X509_STORE_set_default_paths` を使う。BoringSSL の既定パスは `/etc/ssl/cert.pem` と `/etc/ssl/certs` であり、Debian / Ubuntu 系ではシステム CA が載ることが多い。ただしハードコード依存が残っており、0035 の方針と一致しない。

## 設計方針

- 親 0035 が用意するシステム CA 読み込みの差し込み口を Linux 向けに実装する
- 信頼アンカーはディストリビューションの CA バンドル（例: `/etc/ssl/certs/ca-certificates.crt` や OpenSSL 既定パス）から読む
- `X509_STORE_set_default_paths` のみに暗黙依存せず、成功・失敗をログで追跡できるようにする
- `ca_cert` 指定時はシステム CA を混ぜない（親の契約）

対象ビルドターゲットの例: `ubuntu-22.04_x86_64` / `ubuntu-24.04_x86_64` / `ubuntu-22.04_armv8` / `ubuntu-24.04_armv8` / `raspberry-pi-os_armv8`

## 完了条件

- Linux で `ca_cert` 未指定時、システム CA のみ（親方針どおりハードコードなし）で公開 CA の検証が通る
- システム CA の読み込み失敗時は検証失敗となり、失敗理由がログに残る
- `ca_cert` 指定時の挙動が親 0035 の契約どおりである
- 親 0035 の共通差し込み口を使っている

## 解決方法

1. Linux 向けにシステム CA を `X509_STORE` へ載せる実装を追加する
2. バンドルファイルとハッシュディレクトリのどちらを使うかは、実機・CI の Ubuntu / Raspberry Pi OS で確認して決める
3. sumomo 等で WSS（必要なら TURN-TLS）の接続確認を行う

## 関連

- 親: `issues/0035-change-tls-trust-store-system-ca.md`
