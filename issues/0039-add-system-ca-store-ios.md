# iOS でシステム CA を TLS 検証の信頼ストアに使う

- Priority: Medium
- Created: 2026-07-16
- Completed: {YYYY-MM-DD}
- Model: Composer 2.5
- Branch: feature/add-system-ca-store-ios
- Polished: 2026-07-16

## 目的

iOS 上の WSS / TURN-TLS 証明書検証で、Apple の公開 Trust Store（iOS がプリインストールする信頼ルート CA の一覧）に相当する PEM を SDK にバンドルし、それを信頼の根拠として使う。親 issue 0035 の iOS 実装。

## 優先度根拠

Medium。親 0035 と同じ。iOS では macOS の `SecTrustCopyAnchorCertificates` に相当するシステム信頼アンカー列挙 API が公開されていない（Apple SDK ヘッダで `__IPHONE_NA` により iOS 未提供と明示、0037 の補足参照）。BoringSSL の `X509_STORE_set_default_paths` も iOS では実質無効。そのため iOS だけは「Apple が公開している iOS Trust Store の PEM を SDK にバンドルする」方式で、他 OS の「システムから取得」と同等の信頼範囲を提供する。

## 現状

親 0035 の PR がマージされた後、iOS ターゲットは共通差し込み口 `SSLVerifier::LoadSystemSSLRootCertificates(X509_STORE*)` を経由するが、実装は `src/ssl_verifier_stub.cpp` の暫定実装（現行 4 段ロード相当）に閉じ込められている。本 issue で `src/ssl_verifier_ios.cpp` を追加し、iOS ターゲットの `LoadSystemSSLRootCertificates` を Apple 公開 Trust Store の PEM バンドル読み込みに置き換える。

sumomo は iOS 非対応だが、SDK 本体の `ios` ターゲットは WSS / TURN-TLS 検証を含む。動作検証は `sora-ios-sdk` などの iOS 向けクライアントから間接的に行う。

## 対象ビルドターゲット

| `SORA_TARGET` | `SORA_TARGET_OS` |
|---|---|
| `ios` | `ios` |

`python3 run.py build ios` は Device 向け arm64 のみをビルドする（`run.py` に Simulator SDK 向けの分岐はない）。sora-cpp-sdk リポジトリ単体では Simulator ビルドを提供せず、Simulator 向けのビルドは sora-ios-sdk 側の XCFramework 化で担保する。ランタイム差分はなく、バンドル PEM は不変データで Device / Simulator 共通。

## 想定する動作環境

- iOS 14 以降。webrtc-build の `IOS_DEPLOYMENT_TARGET=14.0` と一致する
- バンドル PEM に ISRG Root X1 が収録されていること（iOS 14 以降の Apple 公開 Trust Store には収録済み）
- Keychain / MDM Configuration Profile 経由で iOS 端末に配布された独自 CA は本実装では反映されない。反映が必要な利用者は `SoraSignalingConfig::ca_cert` への PEM 明示指定で対応する
- macOS 0037 は `SecTrustCopyAnchorCertificates` の System Roots をそのまま使うため、iOS よりも広い trust set になる場合がある（Apple 内部 CA や macOS 専用ルートが含まれる可能性）。同じ Sora サーバーに対して macOS で成功しても iOS で失敗するケースが構造的に発生し得る

## 設計方針

### 信頼アンカーの取得元

Apple が公開している iOS Trust Store のルート CA リストの PEM を SDK にバンドルし、実行時にメモリ上の PEM バッファから列挙する。次の理由:

1. iOS には「システム信頼アンカーを列挙する」公開 API が存在しない（`SecTrustCopyAnchorCertificates` は iOS では利用不可）。実運用で `X509_STORE` にアンカーを流し込む手段はハードコード / バンドル方式に限られる
2. Apple 公開 Trust Store と揃えることで「iOS がプリインストールする信頼範囲」を明確に定義でき、任意のハードコード（Let's Encrypt R3 の埋め込み等）よりトレーサビリティが高い
3. C API のみで完結するため実装ファイルを `.cpp` にできる（Objective-C ランタイム不要、Security.framework のリンクも不要）
4. 親 0035 の `LoadSystemSSLRootCertificates` 契約「1 件以上追加できたら `true`」を計測するには追加件数を数える必要がある。PEM ループで `X509_STORE_add_cert` すればこれが自然に測れる（0036 Linux と同型のパターン）

`SecTrustEvaluateWithError` 等で iOS の信頼判定に委任する経路は親 0035 の `LoadSystemSSLRootCertificates(X509_STORE*)` 契約と衝突するため採用しない（本 issue のスコープ外）。

`X509_STORE_set_default_paths` は本実装では呼ばない。理由はセキュリティ配慮で、環境変数 `SSL_CERT_FILE` / `SSL_CERT_DIR` を注入されると信頼ストアを乗っ取れる経路が残るため、その経路を遮断する（0036 と同じ方針）。

Apple 公開 Trust Store の PEM は各 CA に用途タグ（Web / S/MIME / EAP-TLS 等）が付いているが、本実装は用途を区別せず全証明書を TLS アンカーとして扱う。iOS 標準検証より permissive になる可能性があるが、設計上の割り切りとしてこれを許容する（0037 と同じ整理）。

### PEM バンドルの取得と管理

- 取得先: Apple 公式ドキュメントページ「Lists of available trusted root certificates in iOS」（実 URL は PR 時点の Apple 公式ページを参照する。iOS メジャーバージョンごとに別ページになる場合がある）
- 抽出方針: 本 issue のスコープは「iOS 実機の Trust Store と実質的に一致する PEM バンドルを SDK に含めること」と、実装ファイル・CMake 分岐の書き換えのみ。具体的な抽出コマンド・SHA-256 突合・スクリプト化などの実務手順は本 PR で決定し、PR 本文に記載する。抽出成果物の再現手順（抽出に使ったコマンドライン、Apple 公式ページの参照 URL、参照日、抽出時の macOS / Xcode バージョン）は PR 本文と実装ソース冒頭コメントの両方に記録する
- 配置: 生 PEM は `src/ssl_verifier_ios_trust_store.inc` に分離し、`src/ssl_verifier_ios.cpp` から `#include` する。`.inc` の中身は **スナップショット記録コメント + C++ の生文字列リテラル 1 本** の形式にする（更新時は `.inc` だけを差し替えれば済むようにするため、管理情報を `.inc` 側に集約する）:
  ```
  // Apple Trust Store snapshot: iOS <対応 iOS バージョン>, extracted <YYYY-MM-DD>, source <Apple 公式ページ URL>, macOS <抽出に使った macOS バージョン>
  // SORA_IOS_TRUST_STORE_EXTRACTED: <YYYY-MM-DD>
  R"sora_ios_trust_store(
  -----BEGIN CERTIFICATE-----
  MIIF...
  -----END CERTIFICATE-----
  -----BEGIN CERTIFICATE-----
  ...
  )sora_ios_trust_store"
  ```
  デリミタは PEM 本文と衝突しないよう `sora_ios_trust_store` のように衝突不能な長さの識別子を使う。生文字列リテラルの前に置くコメント行（`//`）は C++ プリプロセッサ上合法であり `#include` 展開を壊さない。`.inc` ファイルの更新はこれらコメントも含めて 1 ファイルの差し替えで完結する
- 内容: iOS 14 以降の Trust Store に含まれる全ルート CA の PEM（`BEGIN CERTIFICATE` ブロックのみ、`TRUSTED CERTIFICATE` は含めない。`PEM_read_bio_X509_AUX` は使わないため）。件数はおおむね 150 前後、平均 1.5-2 KB/PEM で合計 225-300 KB 程度と見込まれる
- スナップショット記録: `src/ssl_verifier_ios_trust_store.inc` の先頭に次の 2 行のコメントを必ず記載する。1 行目は人間向けの文脈情報、2 行目は機械可読な取得日（テストが自動解析するため、フォーマットを厳密に守ること）。

  ```
  // Apple Trust Store snapshot: iOS <対応 iOS バージョン>, extracted <YYYY-MM-DD>, source <Apple 公式ページ URL>, macOS <抽出に使った macOS バージョン>
  // SORA_IOS_TRUST_STORE_EXTRACTED: <YYYY-MM-DD>
  ```

  `SORA_IOS_TRUST_STORE_EXTRACTED` 行の日付は必ず ISO 8601 形式（`YYYY-MM-DD`）とし、1 行目の `extracted` 日付と一致させる。バージョン識別子は Apple 公式ページのタイトル文字列と参照日で一意化する（Apple が独自の `YYYY-MM` 形式を付与するとは限らないため）
- 更新運用: SDK バージョンリリースに追従して手動更新する。SDK リリース手順の checklist に「PEM スナップショットの再抽出と差分確認」を追加する
- 自動化: 本 issue の PR 完了後に別 issue として自動化を起票する
- バイナリサイズへの影響: 静的ライブラリ（`libsora.a`）の `.rodata` セクションに 225-300 KB 追加される。この代償を許容する
- diff レビュー実務対応: `.gitattributes` に `src/ssl_verifier_ios_trust_store.inc linguist-generated=true` を追加して GitHub PR で折り畳ませる。抽出成果物は決定的なもの（並び順・改行コード）にしてバージョン間の diff を最小化する

### 実装骨格

BoringSSL の `BIO_new_mem_buf` → `PEM_read_bio_X509` ループ → `X509_STORE_add_cert` の流れで実装する（0036 Linux とほぼ同型、ただし BIO の生成元がファイルではなくメモリバッファ）。`BIO` の解放漏れを防ぐため、Guard パターン（`src/ssl_verifier.cpp:176-180` の `Guard` 構造体と同型）で `BIO_free` を保証する。`X509` は `X509_STORE_add_cert` 成功・失敗どちらの分岐でも直後に `X509_free` する。

`X509_STORE_add_cert` 失敗時は現行 `LoadBuiltinSSLRootCertificates`（`src/ssl_verifier.cpp:138-142`）と同じく `RTC_LOG(LS_WARNING)` を出して続行する。`SSLVerifier::AddCert` は失敗即 `false` を返し親 0035 の「部分失敗は続行、0 件で `false`」契約と衝突するため使わない。失敗時ログには `X509_NAME_oneline(X509_get_subject_name(cert), ...)` で subject 名を含めて原因追跡できるようにする。`PEM_read_bio_X509` が `nullptr` を返した際は現行 `AddCert` 実装（`src/ssl_verifier.cpp:106-110`）と同じく `ERR_get_error()` を 1 回呼んでエラーキューをクリアしループを抜ける。

関数が `true` を返す経路の return 直前に `RTC_LOG(LS_INFO) << "LoadSystemSSLRootCertificates: added=" << added` を出す。これが「Apple 公開 Trust Store バンドルからアンカーを読んだ件数」の運用証跡になる。

```cpp
// src/ssl_verifier_ios.cpp
#include "sora/ssl_verifier.h"

#include <functional>
#include <utility>

#include <openssl/base.h>
#include <openssl/bio.h>
#include <openssl/err.h>
#include <openssl/pem.h>
#include <openssl/x509.h>

#include <rtc_base/logging.h>

namespace sora {
namespace {

// Apple 公開 Trust Store の PEM バンドル（生文字列リテラルを .inc に分離）
const char kAppleTrustStorePEM[] =
#include "ssl_verifier_ios_trust_store.inc"
    ;

}  // namespace

bool SSLVerifier::LoadSystemSSLRootCertificates(X509_STORE* store) {
  // kAppleTrustStorePEM は const char[] のためコンパイル時に長さが確定する
  BIO* bio = BIO_new_mem_buf(kAppleTrustStorePEM,
                              static_cast<int>(sizeof(kAppleTrustStorePEM) - 1));
  if (bio == nullptr) {
    RTC_LOG(LS_ERROR)
        << "LoadSystemSSLRootCertificates: BIO_new_mem_buf failed";
    return false;
  }
  struct Guard {
    std::function<void()> f;
    Guard(std::function<void()> f) : f(std::move(f)) {}
    ~Guard() { f(); }
  };
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
        << "LoadSystemSSLRootCertificates: no certificates loaded from Apple Trust Store bundle";
    return false;
  }
  RTC_LOG(LS_INFO)
      << "LoadSystemSSLRootCertificates: added=" << added;
  return true;
}

}  // namespace sora
```

### CMakeLists.txt の変更

親 0035 が用意した「共通差し込み口の切り替え分岐」（`SORA_SYSTEM_CA_IMPL` を選ぶ if / elseif ブロック）の `elseif (SORA_TARGET_OS STREQUAL "ios")` ブロックの `set(SORA_SYSTEM_CA_IMPL src/ssl_verifier_stub.cpp)` を `set(SORA_SYSTEM_CA_IMPL src/ssl_verifier_ios.cpp)` に書き換えるのみ。既存の iOS プラットフォーム分岐（`CMakeLists.txt:452-488` 付近）には手を入れない（Security.framework などの追加リンクは不要、BoringSSL のみで完結）。

### スレッド安全性

`VerifyX509`（`src/ssl_verifier.cpp:149`）は毎回 `X509_STORE_new()` で新規ストアを作り、これに対して `LoadSystemSSLRootCertificates` を呼ぶ。1 スレッドが 1 ストアを扱う関係のため、`X509_STORE_add_cert` の並列競合は発生しない。`BIO_new_mem_buf` / `PEM_read_bio_X509` は独立のリソースを扱うため BoringSSL 慣行上スレッドセーフ。`kAppleTrustStorePEM` は read-only の静的 const データで並列読み出しに関して安全。

### 鮮度チェック（Trust Store バンドルの有効期限切れ検出）

- `src/ssl_verifier/ssl_verifier_ios_trust_store.inc` の `SORA_IOS_TRUST_STORE_EXTRACTED` 行から取得日を抽出し、現在日付との差が一定日数を超過していたらテストを失敗させる
- 許容最大経過日数は 180 日（約 6 か月）とする
- チェック用スクリプト（例: `tools/check_trust_store_freshness.py`）を新規追加し、CI で実行する
- Apple が Trust Store を更新していない期間に誤検出しないよう、許容日数は十分に長くとる
- 緊急の手動更新が必要な場合は `SORA_IOS_TRUST_STORE_EXTRACTED` の日付を手動で更新する（再抽出が不要な場合に限る）。その場合は必ず PR 本文に更新理由を記載する

## 完了条件

- `src/ssl_verifier_ios.cpp` が新規追加され、`SSLVerifier::LoadSystemSSLRootCertificates(X509_STORE*)` の iOS 実装を `SSLVerifier::` メンバ関数として保持している
- `src/ssl_verifier_ios_trust_store.inc` が新規追加され、先頭にスナップショット記録コメント（人間向けの文脈行 + 機械可読な `SORA_IOS_TRUST_STORE_EXTRACTED` 行）+ Apple 公開 Trust Store の PEM バンドルを生文字列リテラル 1 本の形式で保持している
- 鮮度チェック用スクリプト `tools/check_trust_store_freshness.py` が新規追加され、`SORA_IOS_TRUST_STORE_EXTRACTED` の日付を読み取り、取得日から 180 日超過でエラーを返す
- `CMakeLists.txt` の共通差し込み口の `elseif (SORA_TARGET_OS STREQUAL "ios")` ブロックの `set(SORA_SYSTEM_CA_IMPL src/ssl_verifier_stub.cpp)` が `set(SORA_SYSTEM_CA_IMPL src/ssl_verifier_ios.cpp)` に書き換わっている
- 既存の iOS プラットフォーム分岐は変更されていない（Security.framework 等の追加リンクなし）
- 親 0035 の `LoadSystemSSLRootCertificates` 契約を満たしている（1 件以上追加で `true` / 部分失敗は `RTC_LOG(LS_WARNING)` 続行 / 0 件は `RTC_LOG(LS_ERROR)` で `false` / キャッシュなし / スレッドセーフ）
- iOS 実装は `SSLVerifier::AddCert` / `X509_STORE_set_default_paths` / Security.framework の C API を呼ばない
- 関数が `true` を返す経路の return 直前に `RTC_LOG(LS_INFO) << "LoadSystemSSLRootCertificates: added=" << added` が 1 回出力される
- テスト戦略節の全項目（ビルド確認・接続確認・回帰確認・証跡取得）を実施し、証跡ログを PR 本文に添付している
- `CHANGES.md` に `[CHANGE]` エントリを 1 本追加し、次を記載する: (a) iOS の TLS 検証を Apple 公開 Trust Store のバンドル PEM に切り替えた旨、(b) 対応 iOS バージョン（iOS 14 以降）と Trust Store スナップショット日付、(c) Keychain / MDM Configuration Profile 経由で配布された独自 CA は反映されない旨、(d) 独自 CA は `SoraSignalingConfig::ca_cert` で明示指定する旨、(e) Trust Store のバンドル PEM は SDK バージョンリリースに追従して手動更新される旨

## 解決方法

1. `src/ssl_verifier_ios_trust_store.inc` を新規追加し、先頭のスナップショット記録コメント（人間向けの文脈行 + 機械可読な `SORA_IOS_TRUST_STORE_EXTRACTED` 行）+ Apple 公開 Trust Store の PEM バンドルを生文字列リテラル 1 本の形式で配置する
2. `src/ssl_verifier_ios.cpp` を新規追加し、上記の設計方針・実装骨格に従って `SSLVerifier::LoadSystemSSLRootCertificates(X509_STORE*)` を実装する。`.inc` は `#include` で読み込む
3. `tools/check_trust_store_freshness.py` を新規追加し、`src/ssl_verifier/ssl_verifier_ios_trust_store.inc` から `SORA_IOS_TRUST_STORE_EXTRACTED` 行を読み取って 180 日超過を検出するスクリプトを実装する
4. `CMakeLists.txt` の共通差し込み口の iOS 分岐で `set` 行を `src/ssl_verifier_ios.cpp` に書き換える
5. テスト戦略節に従い、ビルド確認・接続確認・回帰確認・証跡取得を行う
6. `CHANGES.md` に `[CHANGE]` エントリを追加する

## 変更対象ファイル

- `src/ssl_verifier_ios.cpp`（新規追加）
- `src/ssl_verifier_ios_trust_store.inc`（新規追加、Apple 公開 Trust Store の PEM バンドル）
- `tools/check_trust_store_freshness.py`（新規追加、PEM バンドルの取得日鮮度チェック）
- `CMakeLists.txt`（共通差し込み口の `set` 行 1 行を書き換え）
- `CHANGES.md`（`[CHANGE]` エントリ追加）

`include/sora/ssl_verifier.h` / `src/ssl_verifier.cpp` / `src/ssl_verifier_stub.cpp` / `src/websocket.cpp` / `src/rtc_ssl_verifier.cpp` は本 issue では変更しない。既存の iOS プラットフォーム分岐（`CMakeLists.txt:452-488` 付近）も変更しない。

## テスト戦略

### ビルド確認

- `python3 run.py build ios` が通ることを確認する（Device 向け arm64 のみ、`run.py` の現行仕様に沿う）

### 接続確認

- sora-ios-sdk の WSS で Sora へ接続する任意のサンプルアプリを通じて、Sora Labo 相当の公開 CA サーバー（Let's Encrypt 系、信頼アンカーは ISRG Root X1）に対して WSS で接続できることを確認する。実 CA / 実サーバーを使う（AGENTS.md「モックやスタブは絶対に利用しないこと」に従う）
- TURN-TLS 経路も同経由で通ることを確認する（Sora のサーバー設定で TURN-TLS を強制する）
- 使用する sora-ios-sdk のバージョンと具体的なサンプルアプリ名は PR 作成時に決めて PR 本文に記載する

### 回帰確認

- `SoraSignalingConfig::ca_cert` 明示指定時と `insecure == true` の既存挙動が回帰していないことを、既存 E2E テスト（`e2e-test/` 配下）を CLAUDE.md 記載の形式（`uv run --directory=e2e-test pytest ... -v -s --timeout=60`）で回して確認する。iOS ターゲットは pytest 対象外の場合があるため、その場合は sora-ios-sdk 側での回帰確認手順を PR 本文に記載する

### 証跡取得（Apple Trust Store バンドルから読んだことの実証）

- iOS 側で `RTC_LOG` レベルを `LS_INFO` 以上に設定して起動し、WSS 接続成功時に `LoadSystemSSLRootCertificates: added=<N>` が Xcode Console / `os_log` に出力されることを確認する
- N が 1 以上であれば、バンドル PEM から少なくとも 1 件のアンカーを読み込み、それにより接続の chain building が成功したことになる
- iOS では Linux の Docker 隔離のような「バンドル PEM を無効化」する手段が事実上ないため、失敗ケースの実証は行わず、成功時ログを証跡とする方針を採る（0037 / 0038 と同じ整理）
- ログ抜粋を PR 本文に添付する

### 鮮度チェック（取得日からの経過日数確認）

- `tools/check_trust_store_freshness.py` を実行し、`src/ssl_verifier/ssl_verifier_ios_trust_store.inc` の `SORA_IOS_TRUST_STORE_EXTRACTED` の日付から 180 日以上経過していないか確認する
- 180 日超過の場合はスクリプトが非ゼロの終了コードを返す
- CI ワークフローにこのチェックを組み込む

## 関連

- 親: `issues/0035-change-tls-trust-store-system-ca.md`
- 兄弟: `issues/0036-add-system-ca-store-linux.md` / `issues/0037-add-system-ca-store-macos.md` / `issues/0038-add-system-ca-store-windows.md` / `issues/0040-add-system-ca-store-android.md`

### 補足

- Apple 公開 Trust Store のバンドル PEM 自動更新は本 issue のスコープ外。本 issue の PR 完了後に自動化 issue を別途起票する
