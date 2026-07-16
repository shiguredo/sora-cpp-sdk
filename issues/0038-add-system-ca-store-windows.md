# Windows でシステム CA を TLS 検証の信頼ストアに使う

- Priority: Medium
- Created: 2026-07-16
- Completed: {YYYY-MM-DD}
- Model: Composer 2.5
- Branch: feature/add-system-ca-store-windows
- Polished: 2026-07-16

## 目的

Windows 上の WSS / TURN-TLS 証明書検証で、Windows 証明書ストアの `ROOT`（信頼されたルート証明機関）ストアに登録されたシステム標準アンカーを信頼の根拠として使う。親 issue 0035 の Windows 実装。

`ROOT` ストアには Windows Update の AuthRoot 機構経由で配布される Microsoft Root Certificate Program のルート CA、および Group Policy / Enterprise 経由で配布されるルート CA が統合された形で登録される。

## 優先度根拠

Medium。親 0035 と同じ。Windows には `/etc/ssl/...` 相当のパスがなく、BoringSSL の `X509_STORE_set_default_paths` は Windows では実質無効。現行は埋め込みルート頼みになっており、企業環境の社内ルート CA は Windows 証明書ストア経由で配られるのが標準運用であることから、システム CA 対応の効果が大きい。

## 現状

親 0035 の PR がマージされた後、Windows ターゲットは共通差し込み口 `SSLVerifier::LoadSystemSSLRootCertificates(X509_STORE*)` を経由するが、実装は `src/ssl_verifier_stub.cpp` の暫定実装（現行 4 段ロード相当）に閉じ込められている。本 issue で `src/ssl_verifier_windows.cpp` を追加し、Windows ターゲットの `LoadSystemSSLRootCertificates` を Windows 証明書ストア経由の `ROOT` 読み込みに置き換える。

## 対象ビルドターゲット

| `SORA_TARGET` | `SORA_TARGET_OS` |
|---|---|
| `windows_x86_64` | `windows` |

## 想定する動作環境

- Windows 10 以降（`_WIN32_WINNT=0x0A00` に対応、`CMakeLists.txt:276` 付近）
- `ROOT` ストアに ISRG Root X1 が収録されていること。過去に Windows Update または AuthRoot 経由で `ROOT` ストアの更新を受けた履歴があれば反映済み。本実装は `CertOpenSystemStoreW` で既にストアに置かれているアンカーを事前列挙するため、TLS 検証時点で AuthRoot 自動更新が有効か無効かは問わない

これらを満たさない環境（新規セットアップ直後で `ROOT` ストア更新履歴がない、WSUS 経由で AuthRoot 未配信のまま初期構成のみで運用している企業ネットワーク等）では `LoadSystemSSLRootCertificates` が `false` を返し検証失敗になる。回避手段は `SoraSignalingConfig::ca_cert` への PEM 明示指定。

## 設計方針

### 信頼アンカーの取得元

Windows CryptoAPI の `CertOpenSystemStoreW(NULL, L"ROOT")` が返す `ROOT` ストアを唯一の取得元にする。次の理由:

1. `ROOT` ストアは Windows の証明書ストアサービスによって以下 5 経路の内容が自動的にマージされた仮想ビューを返すため、Microsoft 管理のルート集合と企業配布のルート集合の両方を単一 API で取得できる
   - `HKCU\Software\Microsoft\SystemCertificates\Root`
   - `HKLM\Software\Microsoft\SystemCertificates\Root`
   - `HKCU\Software\Policies\Microsoft\SystemCertificates\Root`
   - `HKLM\Software\Policies\Microsoft\SystemCertificates\Root`
   - `HKLM\Software\Microsoft\EnterpriseCertificates\Root`
2. C API のみで完結するため実装ファイルを `.cpp` にできる（Objective-C++ のような別種の翻訳単位を必要としない）
3. `CertOpenStore` の複雑な引数指定を回避できる（本用途では `ROOT` の統合ビューが取れれば十分）

`CA`（中間証明機関）ストアと `AuthRoot`（AutoUpdate 用待機リスト）ストアは使わない。TLS の chain building は基本的に対向サーバが中間 CA を送るためルートのみで完結し、他 OS 実装（Linux は `ca-certificates.crt`、macOS は System Roots）とも方針が揃う。

macOS が MDM Configuration Profile の CA を反映しない判断とは異なり、Windows では Group Policy / Enterprise 経由で `ROOT` に配布された企業ルート CA も自動的に反映される（上記 5 経路の仮想ビューに含まれるため）。

`ROOT` ストアには上記 5 経路のマージ結果として同一 CA が複数エントリで含まれることがある（例: HKLM と HKCU の両方に同一 CA が登録される運用）。この場合 `X509_STORE_add_cert` は 2 回目以降 `X509_R_CERT_ALREADY_IN_HASH_TABLE` エラーで 0 を返し、実装は `RTC_LOG(LS_WARNING)` を出して続行する。これは Windows 特有の正常動作なので、証跡ログに同一 subject の WARNING が繰り返し出ても異常ではない。

`X509_STORE_set_default_paths` は本実装では呼ばない。理由はセキュリティ配慮で、環境変数 `SSL_CERT_FILE` / `SSL_CERT_DIR` を注入されると信頼ストアを乗っ取れる経路が残るため、その経路を遮断する（0036 / 0037 と同じ方針）。

### 実装骨格

Windows CryptoAPI の `CertOpenSystemStoreW` → `CertEnumCertificatesInStore` ループ → DER 取り出し → `d2i_X509` → `X509_STORE_add_cert` の流れで実装する。`HCERTSTORE` の解放漏れを防ぐため、Guard パターン（`src/ssl_verifier.cpp:176-180` の `Guard` 構造体と同型）で `CertCloseStore(h_store, 0)` を保証する。`X509` は `X509_STORE_add_cert` 成功・失敗どちらの分岐でも直後に `X509_free` する。

`X509_STORE_add_cert` 失敗時は現行 `LoadBuiltinSSLRootCertificates`（`src/ssl_verifier.cpp:138-142`）と同じく `RTC_LOG(LS_WARNING)` を出して続行する。`SSLVerifier::AddCert` は失敗即 `false` を返し親 0035 の「部分失敗は続行、0 件で `false`」契約と衝突するため使わない。失敗時ログには `X509_NAME_oneline(X509_get_subject_name(cert), ...)` で subject 名を含めて原因追跡できるようにする。`d2i_X509` が `nullptr` を返した場合は現行 `AddCert` と同じ扱い（`ERR_get_error()` を 1 回呼んでエラーキューを部分的にクリアし、次の証明書に進む）にする。

`CertEnumCertificatesInStore` は「次回呼び出しで前回の `PCCERT_CONTEXT` が自動的に解放される」仕様のため、ループ中に個別の `CertFreeCertificateContext` は呼ばない。

関数が `true` を返す経路の return 直前に成功時ログを出す。これが「Windows 証明書ストア経由でアンカーを読んだ件数」の運用証跡になる。

```cpp
// src/ssl_verifier_windows.cpp
#include "sora/ssl_verifier.h"

#include <functional>
#include <utility>

// Windows CryptoAPI
#include <windows.h>
#include <wincrypt.h>

#include <openssl/err.h>
#include <openssl/x509.h>

#include <rtc_base/logging.h>

namespace sora {

bool SSLVerifier::LoadSystemSSLRootCertificates(X509_STORE* store) {
  // 第 1 引数 hProv は MSDN 仕様どおり NULL を渡す（本引数は使用されない）
  // 第 2 引数 L"ROOT" は Windows の「信頼されたルート証明機関」ストア
  HCERTSTORE h_store = CertOpenSystemStoreW(NULL, L"ROOT");
  if (h_store == NULL) {
    // GetLastError は後続の Win32 呼び出しで上書きされる可能性があるため即時に取得する
    DWORD err = GetLastError();
    RTC_LOG(LS_ERROR)
        << "LoadSystemSSLRootCertificates: CertOpenSystemStoreW failed: last_error="
        << err;
    return false;
  }
  struct Guard {
    std::function<void()> f;
    Guard(std::function<void()> f) : f(std::move(f)) {}
    ~Guard() { f(); }
  };
  Guard store_guard([h_store]() { CertCloseStore(h_store, 0); });

  int added = 0;
  // CertEnumCertificatesInStore は次回呼び出しで前回の PCCERT_CONTEXT を
  // 自動解放するため、ループ中の CertFreeCertificateContext は呼ばない
  PCCERT_CONTEXT ctx = nullptr;
  while ((ctx = CertEnumCertificatesInStore(h_store, ctx)) != nullptr) {
    // dwCertEncodingType には通常 X509_ASN_ENCODING (0x1) のみが入るが、
    // 将来の CryptoAPI 拡張で他エンコーディングが混ざった場合の防御としてビット判定する
    if ((ctx->dwCertEncodingType & X509_ASN_ENCODING) == 0) {
      continue;
    }
    const unsigned char* p = ctx->pbCertEncoded;
    // d2i_X509 は戻り時点で pbCertEncoded のバイト列のパースを完了しているため、
    // 以降 ctx（および ctx が指すバイト列）の寿命は気にしなくてよい
    X509* cert = d2i_X509(nullptr, &p, static_cast<long>(ctx->cbCertEncoded));
    if (cert == nullptr) {
      // d2i_X509 失敗でエラーキューが積まれるため 1 回取り出してクリアする（現行 AddCert と同型）
      ERR_get_error();
      RTC_LOG(LS_WARNING)
          << "LoadSystemSSLRootCertificates: d2i_X509 failed";
      continue;
    }
    int r = X509_STORE_add_cert(store, cert);
    if (r == 0) {
      char subject[256] = {0};
      // subject が 256 バイト超なら切り詰められるが、X509_NAME_oneline は NUL 終端保証あり
      X509_NAME_oneline(X509_get_subject_name(cert), subject, sizeof(subject));
      RTC_LOG(LS_WARNING)
          << "LoadSystemSSLRootCertificates: X509_STORE_add_cert failed: subject="
          << subject;
      // ROOT ストアの 5 経路仮想ビューでは同一 CA が繰り返し追加を試みられ、
      // その都度 X509_R_CERT_ALREADY_IN_HASH_TABLE 等のエラーがキューに積まれるため
      // 次イテレーションの d2i_X509 / X509_STORE_add_cert のエラー報告を汚染しないよう
      // 1 回取り出してクリアする
      ERR_get_error();
    } else {
      ++added;
    }
    X509_free(cert);
  }
  // ループを抜けた時点で ctx == nullptr。MSDN 仕様上 nullptr は「列挙完了」と
  // 「途中エラー」の両方を意味するため GetLastError() で識別する
  DWORD enum_last_error = GetLastError();
  if (enum_last_error != CRYPT_E_NOT_FOUND) {
    RTC_LOG(LS_WARNING)
        << "LoadSystemSSLRootCertificates: CertEnumCertificatesInStore ended abnormally: last_error="
        << enum_last_error;
  }

  if (added == 0) {
    RTC_LOG(LS_ERROR)
        << "LoadSystemSSLRootCertificates: no certificates loaded from Windows ROOT store";
    return false;
  }
  RTC_LOG(LS_INFO)
      << "LoadSystemSSLRootCertificates: added=" << added;
  return true;
}

}  // namespace sora
```

### CMakeLists.txt の変更

CMakeLists.txt には親 0035 が用意した「共通差し込み口の切り替え分岐」（`SORA_SSL_VERIFIER_SOURCES` を選ぶ if / elseif ブロック）と、既存の Windows プラットフォーム分岐（`CMakeLists.txt:239` 付近から始まる `if (SORA_TARGET_OS STREQUAL "windows")` ブロック）の 2 箇所に手を入れる。

- 共通差し込み口の分岐: `elseif (SORA_TARGET_OS STREQUAL "windows")` ブロックの `set(SORA_SSL_VERIFIER_SOURCES src/ssl_verifier.cpp src/ssl_verifier_stub.cpp)` を `set(SORA_SSL_VERIFIER_SOURCES src/ssl_verifier.cpp src/ssl_verifier_windows.cpp)` に書き換える
- 既存の Windows プラットフォーム分岐: `CMakeLists.txt:265` 付近にコメントアウトされている `#    crypt32.lib` の行のコメントを外して有効化する。既存の `Secur32.lib` などと同じ `target_link_libraries(sora PUBLIC ...)` ブロック内に位置しているため、可視性は既存の他 Windows ライブラリと揃って `PUBLIC` になる（`sora` は静的ライブラリのため下流ターゲット `sumomo` 等でも解決する必要がある）。既存の `Secur32.lib` / `winmm.lib` などとスタイルが混在しているため、`crypt32.lib` は既存流に合わせて小文字始まりのままとする

### スレッド安全性

`VerifyX509`（`src/ssl_verifier.cpp:149`）は毎回 `X509_STORE_new()` で新規ストアを作り、これに対して `LoadSystemSSLRootCertificates` を呼ぶ。1 スレッドが 1 ストアを扱う関係のため、`X509_STORE_add_cert` の並列競合は発生しない。Windows CryptoAPI の `CertOpenSystemStoreW` / `CertEnumCertificatesInStore` / `CertCloseStore` は MSDN の Certificate and Certificate Store Functions の記述に基づき、それぞれ独立の `HCERTSTORE` / `PCCERT_CONTEXT` を扱う限りプロセス全体から並列呼び出し可能。本実装は 1 スレッドが独立の `HCERTSTORE` を開いて閉じるためスレッドセーフ。

## 完了条件

- `src/ssl_verifier_windows.cpp` が新規追加され、`SSLVerifier::LoadSystemSSLRootCertificates(X509_STORE*)` の Windows 実装を `SSLVerifier::` メンバ関数として保持している
- `CMakeLists.txt` の共通差し込み口の `elseif (SORA_TARGET_OS STREQUAL "windows")` ブロックの `set(SORA_SSL_VERIFIER_SOURCES src/ssl_verifier.cpp src/ssl_verifier_stub.cpp)` が `set(SORA_SSL_VERIFIER_SOURCES src/ssl_verifier.cpp src/ssl_verifier_windows.cpp)` に書き換わっている
- 既存の Windows プラットフォーム分岐で `CMakeLists.txt:265` 付近の `#    crypt32.lib` のコメントが外れて有効になっている
- 親 0035 の `LoadSystemSSLRootCertificates` 契約を満たしている（1 件以上追加で `true` / 部分失敗は `RTC_LOG(LS_WARNING)` 続行 / 0 件は `RTC_LOG(LS_ERROR)` で `false` / キャッシュなし / スレッドセーフ）
- Windows 実装は `CertOpenSystemStoreW(NULL, L"ROOT")` のみを取得元とし、`CA` / `AuthRoot` ストアや `X509_STORE_set_default_paths` / `SSLVerifier::AddCert` を呼ばない
- 関数が `true` を返す経路（`added >= 1`）で return 直前に `RTC_LOG(LS_INFO) << "LoadSystemSSLRootCertificates: added=" << added` が 1 回出力される
- テスト戦略節の全項目（ビルド確認・接続確認・回帰確認・証跡取得）を実施し、証跡ログを PR 本文に添付している
- `CHANGES.md` に `[CHANGE]` エントリを 1 本追加し、次を記載する: (a) Windows の TLS 検証を OS のシステム CA（`ROOT` ストア）に切り替えた旨、(b) 対応 Windows バージョン（Windows 10 以降）、(c) `ROOT` ストアに必要なルート CA（例: ISRG Root X1）が反映されていない環境では TLS 検証が失敗する旨、(d) 独自 CA は `SoraSignalingConfig::ca_cert` で明示指定する旨

## 解決方法

1. `src/ssl_verifier_windows.cpp` を新規追加し、上記の設計方針・実装骨格に従って `SSLVerifier::LoadSystemSSLRootCertificates(X509_STORE*)` を実装する
2. `CMakeLists.txt` を「### CMakeLists.txt の変更」節の記述に従って更新する
3. テスト戦略節に従い、ビルド確認・接続確認・回帰確認・証跡取得を行う
4. `CHANGES.md` に `[CHANGE]` エントリを追加する

## 変更対象ファイル

- `src/ssl_verifier_windows.cpp`（新規追加）
- `CMakeLists.txt`（共通差し込み口の `set` 行書き換えと `crypt32.lib` のコメント解除）
- `CHANGES.md`（`[CHANGE]` エントリ追加）

`include/sora/ssl_verifier.h` / `src/ssl_verifier.cpp` / `src/ssl_verifier_stub.cpp` / `src/websocket.cpp` / `src/rtc_ssl_verifier.cpp` は本 issue では変更しない。

## テスト戦略

### ビルド確認

- Windows 実機（または Windows CI）で `python3 run.py build windows_x86_64` が通ることを確認する
- ローカルに Windows 実機がない場合は GitHub Actions の Windows ビルドジョブに担保させ、該当 CI 実行の URL を PR 本文に添える

### 接続確認

- sumomo で Sora Labo 相当の公開 CA サーバー（Let's Encrypt 系、信頼アンカーは ISRG Root X1）に対して WSS で接続できることを確認する。実 CA / 実サーバーを使う（AGENTS.md「モックやスタブは絶対に利用しないこと」に従う）
- TURN-TLS 経路も同 sumomo 経由で通ることを確認する
- sumomo の実行ロール（sendonly / recvonly / sendrecv）と TURN-TLS 強制オプションは PR 作成時に決めて PR 本文に記載する

### 回帰確認

- `SoraSignalingConfig::ca_cert` 明示指定時と `insecure == true` の既存挙動が回帰していないことを、既存 E2E テスト（`e2e-test/` 配下）を CLAUDE.md 記載の形式（`uv run --directory=e2e-test pytest ... -v -s --timeout=60`）で回して確認する。回すテストケースの具体名は PR 作成時に PR 本文に記載する

### 証跡取得（Windows ROOT ストアから読んだことの実証）

- Windows 実機で以下の手順を実行する:
  1. Windows 実機で `python3 examples/sumomo/run.py build windows_x86_64` により sumomo をビルドする（Windows でしかビルドできないため、必要なら別の Windows マシンでビルドしたバイナリ `_build\windows_x86_64\release\sumomo\sumomo.exe` を対象 Windows 実機に持ち込む）
  2. cmd.exe から `sumomo.exe … --log-level info 2> sumomo.stderr.log` の形式で起動し、WSS 経路または TURN-TLS 経路で Sora Labo 相当のサーバーに接続する。PowerShell は stderr リダイレクトが UTF-16 になり grep や貼り付けが崩れやすいため、cmd.exe を推奨する
  3. `sumomo.stderr.log` に `LoadSystemSSLRootCertificates: added=<N>` が出力されることを確認する
- N が 1 以上であれば、`ROOT` ストアから少なくとも 1 件のアンカーを読み込み、それにより接続の chain building が成功したことになる（Sora Labo の TLS 証明書は ISRG Root X1 に連なるため、他の信頼源がない状態で成功する事実が `ROOT` ストア経由の実証になる）
- 同一 subject の `X509_STORE_add_cert failed` WARNING が複数回出るのは、`ROOT` ストアの 5 経路仮想ビューで同一 CA が複数エントリとして返される正常動作である（設計方針節参照）
- 実行ログ（`sumomo.stderr.log`）を PR 本文に添付する
- Windows では Linux の Docker 隔離のような「`ROOT` ストアを一時的に無効化」する手段が事実上ないため、失敗ケースの実証は行わず、成功時ログを証跡とする方針を採る（0037 と同じ整理）

## 関連

- 親: `issues/0035-change-tls-trust-store-system-ca.md`
- 兄弟: `issues/0036-add-system-ca-store-linux.md` / `issues/0037-add-system-ca-store-macos.md` / `issues/0039-add-system-ca-store-ios.md` / `issues/0040-add-system-ca-store-android.md`
