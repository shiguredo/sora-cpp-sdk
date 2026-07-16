# macOS でシステム CA を TLS 検証の信頼ストアに使う

- Priority: Medium
- Created: 2026-07-16
- Completed: {YYYY-MM-DD}
- Model: Composer 2.5
- Branch: feature/add-system-ca-store-macos
- Polished: 2026-07-16

## 目的

macOS 上の WSS / TURN-TLS 証明書検証で、Keychain の System Roots に登録されたシステム標準アンカーを信頼の根拠として使う。親 issue 0035 の macOS 実装。

Keychain のユーザ独自 CA や Admin ドメインの trust settings は含めない（他 OS 実装と方針を揃える。独自 CA は `SoraSignalingConfig::ca_cert` への PEM 明示指定で代替する）。

## 優先度根拠

Medium。親 0035 と同じ。macOS には `/etc/ssl/cert.pem` が存在するが、これは古い OpenSSL 互換のスナップショットで Keychain の変更を常時反映するわけではない。BoringSSL の `X509_STORE_set_default_paths` に頼るとこの静的スナップショットに縛られてしまい、System Roots の実態と乖離する。Security.framework 経由で Keychain の System Roots を読み込むことで、Apple が管理する信頼設定と一致させる。

## 現状

親 0035 の PR がマージされた後、macOS ターゲットは共通差し込み口 `SSLVerifier::LoadSystemSSLRootCertificates(X509_STORE*)` を経由するが、実装は `src/ssl_verifier_stub.cpp` の暫定実装（現行 4 段ロード相当）に閉じ込められている。本 issue で `src/ssl_verifier_macos.cpp` を追加し、macOS ターゲットの `LoadSystemSSLRootCertificates` を Security.framework 経由の System Roots 読み込みに置き換える。

## 対象ビルドターゲット

| `SORA_TARGET` | `SORA_TARGET_OS` |
|---|---|
| `macos_arm64` | `macos` |

`macos_x86_64` は `run.py` の `AVAILABLE_TARGETS` に含まれておらず、`python3 run.py build macos_x86_64` は失敗するためサポート対象外（親 0035 の対象ビルドターゲット表と一致）。

## 想定する動作環境

- macOS Sonoma (14.x) 以降。webrtc-build の `MACOS_DEPLOYMENT_TARGET=14` と一致する
- Keychain の System Roots に ISRG Root X1 が収録されていること（Sonoma (14.x) 以降はいずれも収録済み）

## 設計方針

### 信頼アンカーの取得元

Security.framework の `SecTrustCopyAnchorCertificates(CFArrayRef*)` が返すデフォルト信頼アンカー集合（Apple 管理の System Roots キーチェーンの内容にほぼ相当）を唯一の取得元にする。次の理由:

1. 親 0035 の `LoadSystemSSLRootCertificates` 契約「1 件以上追加できたら `true`」を計測するには追加件数を数える必要がある。`SecTrustCopyAnchorCertificates` が返す CFArray をループしながら `X509_STORE_add_cert` すれば、これが自然に測れる
2. Apple が管理する信頼アンカー集合を正確に反映する
3. C API のみで完結するため実装ファイルを `.cpp` にできる（Objective-C ランタイム不要）

現行 SDK ヘッダ（`Security.framework/Headers/SecTrust.h`）では `SecTrustCopyAnchorCertificates` は `__OSX_AVAILABLE_STARTING(__MAC_10_3, __IPHONE_NA)` のみで deprecated 属性は付いていない（macOS 15 SDK 時点）。本 issue の対象範囲では警告なくビルドできる。

`SecTrustSettingsCopyCertificates(kSecTrustSettingsDomainAdmin/User)` は使わない。Keychain のユーザ・管理者ドメインの trust settings を反映させると、エンドユーザ側からの信頼注入経路が広がり、独自 CA の扱いが他 OS 実装（Linux は `ca-certificates.crt` のみ、独自 CA は `ca_cert` で明示指定）と一貫しなくなる。MDM Configuration Profile で配布された CA は `SecTrustCopyAnchorCertificates` が返す System Roots（Apple 出荷時アンカー）ではなく Admin / User ドメインの trust settings 側に置かれるため、本実装では反映されない。それらを信頼させたい利用者は `SoraSignalingConfig::ca_cert` に PEM を明示指定する。

`SecTrustCopyAnchorCertificates` はアンカー証明書そのものを返すだけで、各証明書に紐づく per-cert trust policy（例: 特定ルートを S/MIME のみ許可し TLS では不信頼にする設定など）を反映しない。返ってきた全証明書を TLS 検証用アンカーとして `X509_STORE_add_cert` に流し込む本実装は、macOS 標準の TLS 検証より permissive になる可能性がある（例: TLS では拒否設定された root が TLS 検証で通ってしまう）。設計上の割り切りとしてこれを許容する。

`X509_STORE_set_default_paths` は本実装では呼ばない。理由はセキュリティ配慮で、環境変数 `SSL_CERT_FILE` / `SSL_CERT_DIR` を注入されると信頼ストアを乗っ取れる経路が残るため、その経路を遮断する（0036 と同じ方針）。

### 実装骨格

Security.framework の C API `SecTrustCopyAnchorCertificates` → CFArray ループ → `SecCertificateCopyData` → `d2i_X509` → `X509_STORE_add_cert` の流れで実装する。CFArrayRef の解放漏れを防ぐため、Guard パターン（`src/ssl_verifier.cpp:176-180` の `Guard` 構造体と同型）で `CFRelease` を保証する。`X509` は `X509_STORE_add_cert` 成功・失敗どちらの分岐でも直後に `X509_free` する。

`X509_STORE_add_cert` 失敗時は現行 `LoadBuiltinSSLRootCertificates`（`src/ssl_verifier.cpp:138-142`）と同じく `RTC_LOG(LS_WARNING)` を出して続行する。`SSLVerifier::AddCert` は失敗即 `false` を返し親 0035 の「部分失敗は続行、0 件で `false`」契約と衝突するため使わない。失敗時ログには `X509_NAME_oneline(X509_get_subject_name(cert), ...)` で subject 名を含めて原因追跡できるようにする。`d2i_X509` が `nullptr` を返した場合は現行 `AddCert` と同じ扱い（`ERR_get_error()` を 1 回呼んでエラーキューを部分的にクリアし、次の証明書に進む）にする。

関数終了直前に成功時ログを出す。これが「Keychain 経由でアンカーを読んだ件数」の運用証跡になる。

```cpp
// src/ssl_verifier_macos.cpp
#include "sora/ssl_verifier.h"

#include <functional>
#include <utility>

#include <CoreFoundation/CoreFoundation.h>
#include <Security/Security.h>

#include <openssl/err.h>
#include <openssl/x509.h>

#include <rtc_base/logging.h>

namespace sora {

bool SSLVerifier::LoadSystemSSLRootCertificates(X509_STORE* store) {
  CFArrayRef anchors = nullptr;
  OSStatus status = SecTrustCopyAnchorCertificates(&anchors);
  if (status != errSecSuccess || anchors == nullptr) {
    RTC_LOG(LS_ERROR)
        << "LoadSystemSSLRootCertificates: SecTrustCopyAnchorCertificates failed: status="
        << status;
    return false;
  }
  struct Guard {
    std::function<void()> f;
    Guard(std::function<void()> f) : f(std::move(f)) {}
    ~Guard() { f(); }
  };
  Guard anchors_guard([anchors]() { CFRelease(anchors); });

  int added = 0;
  CFIndex count = CFArrayGetCount(anchors);
  for (CFIndex i = 0; i < count; ++i) {
    // CFArrayGetValueAtIndex は Get 系のため個別 CFRelease は不要
    SecCertificateRef sec_cert = static_cast<SecCertificateRef>(
        const_cast<void*>(CFArrayGetValueAtIndex(anchors, i)));
    if (sec_cert == nullptr) {
      continue;
    }
    // SecCertificateCopyData は Copy 系のため CFRelease する
    CFDataRef der = SecCertificateCopyData(sec_cert);
    if (der == nullptr) {
      RTC_LOG(LS_WARNING)
          << "LoadSystemSSLRootCertificates: SecCertificateCopyData failed";
      continue;
    }
    const unsigned char* p =
        reinterpret_cast<const unsigned char*>(CFDataGetBytePtr(der));
    // d2i_X509 は der のバイト列を参照して X509 を構築するため、der の解放は d2i_X509 が返るまで遅らせる
    // CFDataGetLength は CFIndex（long）を返すが d2i_X509 の第 3 引数は long なので static_cast で明示する
    X509* cert = d2i_X509(nullptr, &p, static_cast<long>(CFDataGetLength(der)));
    CFRelease(der);
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
        << "LoadSystemSSLRootCertificates: no certificates loaded from Keychain System Roots: count="
        << count;
    return false;
  }
  RTC_LOG(LS_INFO)
      << "LoadSystemSSLRootCertificates: added=" << added;
  return true;
}

}  // namespace sora
```

### CMakeLists.txt の変更

CMakeLists.txt には親 0035 が用意した「共通差し込み口の切り替え分岐」（`SORA_SYSTEM_CA_IMPL` を選ぶ if / elseif ブロック）と、既存の macOS プラットフォーム分岐（`CMakeLists.txt:395` 付近から始まる `elseif (SORA_TARGET_OS STREQUAL "macos")` ブロック）の 2 箇所に手を入れる。

- 共通差し込み口の分岐: 該当する `elseif (SORA_TARGET_OS STREQUAL "macos")` ブロックの `set(SORA_SYSTEM_CA_IMPL src/ssl_verifier_stub.cpp)` を `set(SORA_SYSTEM_CA_IMPL src/ssl_verifier_macos.cpp)` に書き換える
- 既存の macOS プラットフォーム分岐（395 行付近）の `target_link_libraries(sora PUBLIC ...)` ブロック（430 行付近、`AVFoundation` / `AudioToolbox` / `QuartzCore` などが列挙されている）の末尾に `"-framework Security"` を 1 行追加する。可視性は既存の他フレームワークと揃えて `PUBLIC` にする（`sora` は静的ライブラリのため下流ターゲット `sumomo` 等でも解決する必要がある）
- CoreFoundation は Security の依存として自動リンクされるため明示追加はしない

### スレッド安全性

`VerifyX509`（`src/ssl_verifier.cpp:149`）は毎回 `X509_STORE_new()` で新規ストアを作り、これに対して `LoadSystemSSLRootCertificates` を呼ぶ。1 スレッドが 1 ストアを扱う関係のため、`X509_STORE_add_cert` の並列競合は発生しない。Security Services および CoreFoundation の Get/Copy 系関数はプロセス全体から並列呼び出し可能とみなす（Apple の Security ドキュメントでスレッド安全性が明示されており、実務上も広く使われている）。

## 完了条件

- `src/ssl_verifier_macos.cpp` が新規追加され、`SSLVerifier::LoadSystemSSLRootCertificates(X509_STORE*)` の macOS 実装を `SSLVerifier::` メンバ関数として保持している
- `CMakeLists.txt` の共通差し込み口の `elseif (SORA_TARGET_OS STREQUAL "macos")` ブロックの `set(SORA_SYSTEM_CA_IMPL src/ssl_verifier_stub.cpp)` が `set(SORA_SYSTEM_CA_IMPL src/ssl_verifier_macos.cpp)` に書き換わっている
- 既存の macOS プラットフォーム分岐（`CMakeLists.txt:395` 付近から始まる別の `elseif (SORA_TARGET_OS STREQUAL "macos")`）の既存 `target_link_libraries(sora PUBLIC ...)` に `-framework Security` が追加されている
- 親 0035 の `LoadSystemSSLRootCertificates` 契約を満たしている（1 件以上追加で `true` / 部分失敗は `RTC_LOG(LS_WARNING)` 続行 / 0 件は `RTC_LOG(LS_ERROR)` で `false` / キャッシュなし / スレッドセーフ）
- macOS 実装は `SecTrustCopyAnchorCertificates` のみを取得元とし、`SecTrustSettingsCopyCertificates` / `X509_STORE_set_default_paths` / `SSLVerifier::AddCert` を呼ばない
- 関数が `true` を返す経路（`added >= 1`）で return 直前に `RTC_LOG(LS_INFO) << "LoadSystemSSLRootCertificates: added=" << added` が 1 回出力される
- テスト戦略節の全項目（ビルド確認・接続確認・回帰確認・証跡取得）を実施し、証跡ログを PR 本文に添付している
- `CHANGES.md` に `[CHANGE]` エントリを 1 本追加し、次を記載する: (a) macOS の TLS 検証を OS のシステム CA（Keychain System Roots）に切り替えた旨、(b) 対応 macOS バージョン（Sonoma (14.x) 以降）、(c) Keychain の Admin / User ドメインの trust settings と MDM Configuration Profile で配布された CA は反映されず、前提が崩れると TLS 検証が失敗する旨、(d) 独自 CA は `SoraSignalingConfig::ca_cert` で明示指定する旨

## 解決方法

1. `src/ssl_verifier_macos.cpp` を新規追加し、上記の設計方針・実装骨格に従って `SSLVerifier::LoadSystemSSLRootCertificates(X509_STORE*)` を実装する
2. `CMakeLists.txt` を「### CMakeLists.txt の変更」節の記述に従って更新する
3. テスト戦略節に従い、ビルド確認・接続確認・回帰確認・証跡取得を行う
4. `CHANGES.md` に `[CHANGE]` エントリを追加する

## 変更対象ファイル

- `src/ssl_verifier_macos.cpp`（新規追加）
- `CMakeLists.txt`（macOS 分岐の `set` 行書き換えと `-framework Security` 追加）
- `CHANGES.md`（`[CHANGE]` エントリ追加）

`include/sora/ssl_verifier.h` / `src/ssl_verifier.cpp` / `src/ssl_verifier_stub.cpp` / `src/websocket.cpp` / `src/rtc_ssl_verifier.cpp` は本 issue では変更しない。

## テスト戦略

### ビルド確認

- `python3 run.py build macos_arm64` が通ることを確認する

### 接続確認

- sumomo で Sora Labo 相当の公開 CA サーバー（Let's Encrypt 系、信頼アンカーは ISRG Root X1）に対して WSS で接続できることを確認する。実 CA / 実サーバーを使う（AGENTS.md「モックやスタブは絶対に利用しないこと」に従う）
- TURN-TLS 経路も同 sumomo 経由で通ることを確認する
- sumomo の実行ロール（sendonly / recvonly / sendrecv）と TURN-TLS 強制オプションは PR 作成時に決めて PR 本文に記載する

### 回帰確認

- `SoraSignalingConfig::ca_cert` 明示指定時と `insecure == true` の既存挙動が回帰していないことを、既存 E2E テスト（`e2e-test/` 配下）を CLAUDE.md 記載の形式（`uv run --directory=e2e-test pytest ... -v -s --timeout=60`）で回して確認する。回すテストケースの具体名は PR 作成時に PR 本文に記載する

### 証跡取得（Keychain System Roots から読んだことの実証）

- sumomo を `--log-level info` オプション付きで起動し（`sumomo` は `examples/sumomo/` のバイナリ、`--log-level info` は `sumomo.cpp` に実装済み）、WSS 接続成功時に `LoadSystemSSLRootCertificates: added=<N>` が sumomo の stderr に出力されることを確認する。実行例: `./sumomo … --log-level info 2> sumomo.stderr.log`
- N が 1 以上であれば、Keychain の System Roots から少なくとも 1 件のアンカーを読み込み、それにより接続の chain building が成功したことになる（Sora Labo の TLS 証明書は ISRG Root X1 に連なるため、他の信頼源がない状態で成功する事実が Keychain 経由の実証になる）
- 実行ログ（stderr）を PR 本文に添付する
- macOS では Linux の Docker 隔離のような「Keychain を無効化」の手段がないため、失敗ケースの実証は行わず、成功時ログを証跡とする方針を採る

## 関連

- 親: `issues/0035-change-tls-trust-store-system-ca.md`
- 兄弟: `issues/0036-add-system-ca-store-linux.md` / `issues/0038-add-system-ca-store-windows.md` / `issues/0039-add-system-ca-store-ios.md` / `issues/0040-add-system-ca-store-android.md`

### 補足

- `SecTrustCopyAnchorCertificates` は iOS では利用不可（SDK ヘッダの `__OSX_AVAILABLE_STARTING(__MAC_10_3, __IPHONE_NA)` で `__IPHONE_NA` により iOS 未提供と明示されている）。本実装は iOS 0039 には流用できない
