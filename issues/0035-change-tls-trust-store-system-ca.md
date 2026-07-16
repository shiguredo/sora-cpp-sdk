# TLS 検証の信頼ストアをシステム CA + ca_cert に切り替える

- Priority: Medium
- Created: 2026-07-16
- Completed: {YYYY-MM-DD}
- Model: Composer
- Branch: feature/change-tls-trust-store-system-ca
- Polished: {YYYY-MM-DD}

## 目的

WSS / TURN-TLS のサーバー証明書検証で、Let's Encrypt と WebRTC `ssl_roots.h` のハードコードされたルート証明書に依存するのをやめ、OS のシステム CA ストアと `SoraSignalingConfig::ca_cert` による明示指定を信頼の根拠にする。

## 優先度根拠

Medium。公開 CA（Let's Encrypt 等）では現状でも接続できることが多いが、社内 CA・独自 CA・OS 側で管理される信頼設定を反映できない。Windows / iOS / Android では BoringSSL の `/etc/ssl/...` 既定パスが事実上効かず、埋め込みルート頼みになっている。TLS 検証の正しさと運用性の問題であるため Medium。

## 現状

WSS と TURN-TLS はともに最終的に `SSLVerifier::VerifyX509`（`src/ssl_verifier.cpp`）で検証する。

- WSS: `src/websocket.cpp` の verify callback
- TURN-TLS: `RTCSSLVerifier`（`src/rtc_ssl_verifier.cpp`）→ `SSLVerifier::VerifyX509`
- 設定は `SoraSignalingConfig` の `insecure` / `ca_cert`（`include/sora/sora_signaling.h`）

`ca_cert` 未指定かつ `insecure == false` のとき、信頼ストアは次の順で組み立てられる（`src/ssl_verifier.cpp`）:

1. ハードコードされた ISRG Root X1 と Let's Encrypt R3
2. WebRTC `rtc_base/ssl_roots.h`（`LoadBuiltinSSLRootCertificates`）
3. `X509_STORE_set_default_paths`（BoringSSL 既定は `/etc/ssl/cert.pem` と `/etc/ssl/certs`）

`ca_cert` 指定時は指定 PEM のみを使い、上記 1〜3 は読み込まない（この挙動は維持する）。

問題点:

- Keychain / Windows 証明書ストア / Android システム CA を読まない
- OS ごとに信頼できる CA 集合が埋め込みルートに引きずられる
- Let's Encrypt 中間証明書のハードコードはルート信頼の本来の役割ではない

## 設計方針

親 issue（本 issue）で方針と共通インターフェースを決め、OS 固有のシステム CA 読み込みは子 issue で実装する。

### 信頼ストアの方針（`ca_cert` 未指定時）

1. OS のシステム CA ストアから信頼アンカーを読み込む
2. ハードコードされた Let's Encrypt 証明書は追加しない
3. WebRTC `ssl_roots.h` はデフォルトでは追加しない

### `ca_cert` 指定時

現状どおり、指定 PEM（複数可）のみを信頼する。システム CA もハードコードも混ぜない。

### `insecure == true`

現状どおり検証をスキップする。

### 共通構造

`SSLVerifier` に「システム CA を `X509_STORE` へ載せる」処理を差し込める形にする。OS 差分はコンパイル時または薄いラッパに閉じ、WSS / TURN-TLS の呼び出し側（`websocket.cpp` / `rtc_ssl_verifier.cpp`）の API は変えない。

子 issue:

| 子 issue | OS |
|---|---|
| 0036 | Linux |
| 0037 | macOS |
| 0038 | Windows |
| 0039 | iOS |
| 0040 | Android |

本 issue はメタではなく、共通の方針確定と `SSLVerifier` 側のハードコード依存削除・差し込み口の用意を含む。各 OS のローダ本体は子の完了をもって揃う。全子が完了するまで、未実装 OS ではシステム CA 読み込み失敗をエラーにするか、一時的に現行フォールバックを残すかは実装着手時に決める（フォールバックを残す場合は削除期限を子 issue に書く）。

## 完了条件

- `ca_cert` 未指定時に、ハードコードされた Let's Encrypt / WebRTC `ssl_roots.h` を信頼ストアへ追加しない
- システム CA 読み込みの差し込み口が `SSLVerifier` にあり、各 OS 子 issue がそれを実装できる
- `ca_cert` 指定時は指定 PEM のみ、という既存契約が維持されている
- WSS と TURN-TLS の両方で同じ信頼ストア方針が使われる
- 子 issue 0036〜0040 がすべて closed になっている（または本 issue のスコープを共通部のみに絞り、クローズ条件を共通部完了に変更した場合はその条件を満たす）

## 解決方法

1. `src/ssl_verifier.cpp` から `isrg_root` / `lets_encrypt_r3` の埋め込みと、デフォルト経路での `LoadBuiltinSSLRootCertificates` 呼び出しを外す
2. システム CA を `X509_STORE` に載せる関数（例: `LoadSystemSSLRootCertificates`）を宣言し、OS 別実装は子 issue で埋める
3. `X509_STORE_set_default_paths` だけに頼る現行 Linux 寄りの挙動は、0036 で明示的なシステム CA 読み込みに置き換える前提とし、共通部では「システム CA ローダ必須」とする
4. `ca_cert` / `insecure` の分岐は維持する
5. CHANGES.md にはハードコード廃止とシステム CA 利用への変更を記載する（子の OS 対応とまとめてよい）

## 変更対象ファイル（共通部の見込み）

- `src/ssl_verifier.cpp`
- `include/sora/ssl_verifier.h`
- 必要なら OS 別ソース（子 issue で追加）

## テスト戦略

- 公開 CA の WSS / TURN-TLS 接続が、システム CA 経由で成功すること（各 OS 子で確認）
- 独自 CA サーバーに対し、システムに載せていない状態では失敗し、`ca_cert` 指定で成功すること
- `insecure == true` で検証スキップが維持されること
