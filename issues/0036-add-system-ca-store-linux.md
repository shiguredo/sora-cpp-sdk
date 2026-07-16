# Linux でシステム CA を TLS 検証の信頼ストアに使う

- Priority: Medium
- Created: 2026-07-16
- Completed: {YYYY-MM-DD}
- Model: Composer 2.5
- Branch: feature/change-system-ca-store-linux
- Polished: 2026-07-16

## 目的

Linux 上の WSS / TURN-TLS 証明書検証で、OS が提供するシステム CA（`ca-certificates` パッケージが配置する CA バンドル）を信頼アンカーとして使う。親 issue 0035 の Linux 実装。

sora-cpp-sdk がサポートする Linux は Ubuntu 22.04 / 24.04 と Raspberry Pi OS（bookworm 以降）のみで、いずれも Debian 系。RHEL / Fedora / Arch / Alpine 等はサポート対象外とする。親 0035 の CMake 分岐では 5 ターゲットすべて `SORA_TARGET_OS = ubuntu` に集約されるため、本 issue が追加する実装ファイル名は `src/ssl_verifier_ubuntu.cpp` になる（Raspberry Pi OS も `CMakeLists.txt:58-61` で `ubuntu` に集約される）。

## 優先度根拠

Medium。親 0035 と同じ。Linux は `/etc/ssl/certs` が存在する環境が大半で、現行の `X509_STORE_set_default_paths` でも動くケースが多いが、方針として「埋め込みルートではなくシステム CA」を明示し、他 OS 実装とインタフェースを揃える。

## 現状

親 0035 の PR がマージされた後、Linux ターゲットは他 4 OS 用ヘルパー `SSLVerifier::LoadSystemSSLRootCertificates(X509_STORE*)` を経由するが、実装は `src/ssl_verifier_stub.cpp` の暫定実装（現行 4 段ロード相当）に閉じ込められている。本 issue で `src/ssl_verifier_ubuntu.cpp` を追加し、Linux ターゲットの `LoadSystemSSLRootCertificates` を Debian 系の CA バンドル読み込みに置き換える。

## 対象ビルドターゲット

| `SORA_TARGET` | `SORA_TARGET_OS` |
|---|---|
| `ubuntu-22.04_x86_64` | `ubuntu` |
| `ubuntu-24.04_x86_64` | `ubuntu` |
| `ubuntu-22.04_armv8` | `ubuntu` |
| `ubuntu-24.04_armv8` | `ubuntu` |
| `raspberry-pi-os_armv8` | `ubuntu` |

## 想定する動作環境

- `ca-certificates` パッケージが導入され、`/etc/ssl/certs/ca-certificates.crt`（Debian の `update-ca-certificates` が生成する単一 PEM バンドル）が存在すること
- 同バンドルに ISRG Root X1 が収録されていること。Ubuntu 22.04 / 24.04 と Raspberry Pi OS bookworm 以降の `ca-certificates` はいずれも収録済み
- 独自 CA を反映させる場合は、`/usr/local/share/ca-certificates/*.crt` に配置後 `update-ca-certificates` を実行する（Debian 標準運用）

これらを満たさない環境（`ca-certificates` 未導入のミニマル Docker イメージ、Raspberry Pi OS bullseye 以前など）では `LoadSystemSSLRootCertificates` が `false` を返し検証失敗になる。回避手段は `SoraSignalingConfig::ca_cert` への PEM 明示指定。

## 設計方針

### 信頼アンカーの取得元

`/etc/ssl/certs/ca-certificates.crt`（単一ファイル）を唯一の取得元にする。ハッシュディレクトリ（`/etc/ssl/certs/`）は使わない。理由の重要度順:

1. 親 0035 の `LoadSystemSSLRootCertificates` 契約「1 件以上追加できたら `true`」を計測するには追加件数を数える必要がある。バンドルファイルを PEM ループで読む方式ならこれが自然に測れる。BoringSSL の `X509_STORE_load_locations` / `X509_STORE_set_default_paths` は「lookup method を登録するだけで追加件数を返さない」ため契約を満たせない
2. 単一ファイルはクロスコンパイル済みバイナリで実行時に確定パスを開くだけで済み、`c_rehash` の完了状態にも依存しない

### 実装骨格

BoringSSL の `BIO_new_file` → `PEM_read_bio_X509` ループ → `X509_STORE_add_cert` の流れで実装する。BIO の解放漏れを防ぐため、Guard パターン（`src/ssl_verifier.cpp:176-180` に定義されている `Guard` 構造体と同型）で `BIO_free` を保証する。`X509` は `X509_STORE_add_cert` 成功・失敗どちらの分岐でも直後に `X509_free` する。`PEM_read_bio_X509` が `nullptr` を返した際は現行の `AddCert` 実装（`src/ssl_verifier.cpp:106-110`）と同じく `ERR_get_error()` を 1 回呼んでエラーキューをクリアしループを抜ける。

`X509_STORE_add_cert` 失敗時は現行 `LoadBuiltinSSLRootCertificates`（`src/ssl_verifier.cpp:138-142`）と同じく `RTC_LOG(LS_WARNING)` を出して続行する（`AddCert` は失敗即 `false` を返すが、本実装は親 0035 の契約「部分失敗は続行、0 件で `false`」に従うため `AddCert` の失敗ハンドリングとは異なる）。失敗時ログには `X509_NAME_oneline(X509_get_subject_name(cert), ...)` で subject 名を含めて原因追跡できるようにする。

```cpp
// src/ssl_verifier_ubuntu.cpp
#include "sora/ssl_verifier.h"

#include <functional>
#include <utility>

#include <openssl/bio.h>
#include <openssl/err.h>
#include <openssl/pem.h>
#include <openssl/x509.h>

#include <rtc_base/logging.h>

namespace sora {

bool SSLVerifier::LoadSystemSSLRootCertificates(X509_STORE* store) {
  const char* path = "/etc/ssl/certs/ca-certificates.crt";
  BIO* bio = BIO_new_file(path, "r");
  if (bio == nullptr) {
    RTC_LOG(LS_ERROR)
        << "LoadSystemSSLRootCertificates: BIO_new_file failed: path=" << path;
    return false;
  }
  // BIO の解放は必ず通す
  struct Guard {
    std::function<void()> f;
    Guard(std::function<void()> f) : f(std::move(f)) {}
    ~Guard() { f(); }
  };
  // bio は以降再代入されない前提で値捕捉する
  Guard bio_guard([bio]() { BIO_free(bio); });

  int added = 0;
  while (true) {
    X509* cert = PEM_read_bio_X509(bio, nullptr, nullptr, nullptr);
    if (cert == nullptr) {
      ERR_get_error();
      break;
    }
    int r = X509_STORE_add_cert(store, cert);
    if (r == 0) {
      char subject[256] = {0};
      X509_NAME_oneline(X509_get_subject_name(cert), subject, sizeof(subject));
      RTC_LOG(LS_WARNING)
          << "LoadSystemSSLRootCertificates: X509_STORE_add_cert failed: subject="
          << subject;
    } else {
      ++added;
    }
    X509_free(cert);
  }

  if (added == 0) {
    RTC_LOG(LS_ERROR)
        << "LoadSystemSSLRootCertificates: no certificates loaded: path=" << path;
    return false;
  }
  return true;
}

}  // namespace sora
```

`PEM_read_bio_X509_AUX` は使わない（`/etc/ssl/certs/ca-certificates.crt` は Debian の `update-ca-certificates` が `-----BEGIN CERTIFICATE-----` ブロックの単純連結として出力するため、`PEM_read_bio_X509` で十分。`PEM_read_bio_X509_AUX` は `TRUSTED CERTIFICATE` ブロックを対象とする）。

`X509_STORE_set_default_paths` は本実装では呼ばない。理由はセキュリティ配慮で、環境変数 `SSL_CERT_FILE` / `SSL_CERT_DIR` を注入されると信頼ストアを乗っ取れる経路が残るため、その経路を遮断する。

### スレッド安全性

`VerifyX509`（`src/ssl_verifier.cpp:149` の実装）は毎回 `X509_STORE_new()` で新規ストアを作り、これに対して `LoadSystemSSLRootCertificates` を呼ぶ。1 スレッドが 1 ストアを扱う関係のため、`X509_STORE_add_cert` の並列競合は発生しない。`BIO_new_file` / `PEM_read_bio_X509` は独立のリソースを扱うため BoringSSL 慣行上スレッドセーフ。バンドルファイルは read-only オープンのみでファイル IO 競合も発生しない。

## 完了条件

- `src/ssl_verifier_ubuntu.cpp` が新規追加され、`SSLVerifier::LoadSystemSSLRootCertificates(X509_STORE*)` の Linux 実装を `SSLVerifier::` メンバ関数として保持している
- `CMakeLists.txt` の `if (SORA_TARGET_OS STREQUAL "ubuntu")` ブロックの `set(SORA_SSL_VERIFIER_SOURCES src/ssl_verifier.cpp src/ssl_verifier_stub.cpp)` が `set(SORA_SSL_VERIFIER_SOURCES src/ssl_verifier.cpp src/ssl_verifier_ubuntu.cpp)` に書き換わっている（親 0035 の CMake 骨格では Ubuntu は最初の `if` 分岐）
- 親 0035 の `LoadSystemSSLRootCertificates` 契約を満たしている（1 件以上追加で `true` / 部分失敗は `RTC_LOG(LS_WARNING)` 続行 / 0 件は `RTC_LOG(LS_ERROR)` で `false` / キャッシュなし / スレッドセーフ）
- Linux 実装で `X509_STORE_set_default_paths` を呼んでいない
- Linux 実装は `SSLVerifier::AddCert` を呼ばない（`AddCert` は失敗即 `false` のため親 0035 の「部分失敗は続行」契約と衝突する）
- 対象 5 ターゲット全てで `python3 run.py build <target>` が通ること（実施手段はテスト戦略節）
- テスト戦略節の Docker 隔離環境テストで、sumomo の WSS 接続が検証失敗になること、および `RTC_LOG(LS_ERROR)` の英語 1 行ログが出ることの両方を確認し、PR 本文にログ抜粋を添付している
- `CHANGES.md` に `[CHANGE]` エントリを 1 本追加し、次を記載する: (a) Linux の TLS 検証を OS のシステム CA に切り替えた旨、(b) 対応ディストリと下限バージョン（Ubuntu 22.04 / 24.04、Raspberry Pi OS bookworm 以降）、(c) 前提を満たさない環境（`ca-certificates` 未導入、上記より古い Raspberry Pi OS 等）では TLS 検証が失敗する旨、(d) 移行手段として `SoraSignalingConfig::ca_cert` への PEM 明示指定（親 0035 の契約どおり単一の PEM 文字列内に複数証明書を含めてよい）

## 解決方法

1. `src/ssl_verifier_ubuntu.cpp` を新規追加し、上記の設計方針・実装骨格に従って `SSLVerifier::LoadSystemSSLRootCertificates(X509_STORE*)` を実装する
2. `CMakeLists.txt` の `if (SORA_TARGET_OS STREQUAL "ubuntu")` ブロックの `set(SORA_SSL_VERIFIER_SOURCES ...)` 行の 2 番目のファイルを `src/ssl_verifier_stub.cpp` から `src/ssl_verifier_ubuntu.cpp` に書き換える
3. テスト戦略節に従い、ビルド確認・接続確認・回帰確認・システム CA のみでの担保確認を行う
4. `CHANGES.md` に `[CHANGE]` エントリを追加する

## 変更対象ファイル

- `src/ssl_verifier_ubuntu.cpp`（新規追加）
- `CMakeLists.txt`（`if (SORA_TARGET_OS STREQUAL "ubuntu")` ブロックの `set(SORA_SSL_VERIFIER_SOURCES ...)` 行の 2 番目のファイルを書き換え）
- `CHANGES.md`（`[CHANGE]` エントリ追加）

`include/sora/ssl_verifier.h` / `src/ssl_verifier.cpp` / `src/ssl_verifier_stub.cpp` / `src/websocket.cpp` / `src/rtc_ssl_verifier.cpp` は本 issue では変更しない。

## テスト戦略

### ビルド確認

- ローカルでは代表 1 ターゲット（`ubuntu-24.04_x86_64`）で `python3 run.py build ubuntu-24.04_x86_64` が通ることを確認する
- 他の 4 ターゲット（`ubuntu-22.04_x86_64` / `ubuntu-22.04_armv8` / `ubuntu-24.04_armv8` / `raspberry-pi-os_armv8`）は CI（GitHub Actions）に担保させる。該当 CI 実行の URL を PR 本文に添える

### 接続確認

- sumomo で Sora Labo 相当の公開 CA サーバーに対して WSS で接続できることを確認する。実 CA / 実サーバーを使う（AGENTS.md「モックやスタブは絶対に利用しないこと」に従う）
- TURN-TLS 経路も同 sumomo 経由で通ることを確認する
- sumomo の実行ロール（sendonly / recvonly / sendrecv）と TURN-TLS 強制オプションは PR 作成時に決めて PR 本文に記載する

### 回帰確認

- `SoraSignalingConfig::ca_cert` 明示指定時と `insecure == true` の既存挙動が回帰していないことを、既存 E2E テスト（`e2e-test/` 配下）を CLAUDE.md 記載の形式（`uv run --directory=e2e-test pytest ... -v -s --timeout=60`）で回して確認する。回すテストケースの具体名は PR 作成時に PR 本文に記載する

### システム CA のみで検証している証跡

- 隔離用の Docker コンテナで sumomo の WSS 接続を試み、システム CA のみで検証していることを実証する。手順:
  1. ホストで `python3 examples/sumomo/run.py build ubuntu-24.04_x86_64` により sumomo をビルドする
  2. `docker run --rm -it -v $(pwd)/_build/ubuntu-24.04_x86_64/release/sumomo:/sumomo:ro ubuntu:24.04 bash` でコンテナを起動する
  3. コンテナ内で `apt-get update && apt-get install -y ca-certificates && update-ca-certificates` を実行する
  4. コンテナ内で `mv /etc/ssl/certs/ca-certificates.crt /tmp/backup.crt` として CA バンドルを退避する
  5. コンテナ内で `/sumomo/sumomo` を実行し、Sora Labo 相当の公開 CA サーバーに WSS 接続を試みる（signaling URL / channel ID / access token は環境変数で渡す）
  6. 接続が検証失敗になること、および `RTC_LOG(LS_ERROR)` の英語 1 行ログ（`"LoadSystemSSLRootCertificates: BIO_new_file failed: path=/etc/ssl/certs/ca-certificates.crt"`）が sumomo の標準エラー出力に出ることの両方を確認する
  7. 実行ログ（stderr）を PR 本文に添付する
- ホスト OS で直接ファイルを退避しない（他プロセスの TLS 通信を巻き込むため）
- Ubuntu 22.04 / Raspberry Pi OS bookworm の代表性: Debian 系は `update-ca-certificates` が生成する `ca-certificates.crt` の形式（`BEGIN CERTIFICATE` ブロックの単純連結）が共通で、実装が touch するのはこのファイルのみのため、Ubuntu 24.04 の隔離テストで他ターゲットを代表できる
- 本試験は意図的に ERROR ログを発生させるため CI には組み込まない（PR 本文への手動証跡添付のみ）

## 関連

- 親: `issues/0035-change-tls-trust-store-system-ca.md`
- 兄弟: `issues/0037-add-system-ca-store-macos.md` / `issues/0038-add-system-ca-store-windows.md` / `issues/0039-add-system-ca-store-ios.md` / `issues/0040-add-system-ca-store-android.md`
