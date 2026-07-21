# iOS でシステム CA を TLS 検証の信頼ストアに使う

- Priority: Medium
- Created: 2026-07-16
- Completed: 2026-07-17
- Model: Composer 2.5
- Branch: feature/change-system-ca-store-ios
- Polished: 2026-07-16

## 目的

iOS 上の WSS / TURN-TLS 証明書検証で、iOS のシステム trust store を信頼アンカーとして使う。親 issue 0035 の iOS 実装。

iOS は Apple のサンドボックス設計上、アプリから system trust store のアンカーを列挙する public API が存在しない（`SecTrustCopyAnchorCertificates` / `SecTrustSettingsCopyCertificates` は macOS 専用）。そのため実装手段としては、他 4 OS のような「アンカー列挙型」（`LoadSystemSSLRootCertificates` でシステム CA を `X509_STORE` に投入）を採らず、「検証委譲型」（`SSLVerifier::VerifyX509` を丸ごと差し替えて Security.framework の `SecTrustEvaluateWithError` に検証を渡す）を採る。信頼の根拠が iOS のシステム trust store になるという結果は他 4 OS と同じ。iOS の実装手段の詳細は親 0035 の「iOS の実装手段」節を参照。

## 優先度根拠

Medium。親 0035 と同じ。iOS の TLS 検証は現状ハードコード PEM + WebRTC `ssl_roots.h` に依存しており、Apple の system trust store が更新されても SDK 側で追従できない。Security.framework に委譲すれば Apple の trust store 更新（CT log、revocation、新 CA 収録など）を自動で取り込める。

## 現状

親 0035 の PR がマージされた後、iOS ターゲットは親 PR 時点で「共通の `src/ssl_verifier.cpp` + 暫定 `src/ssl_verifier_stub.cpp`」の 2 ファイルビルドで、`SSLVerifier::VerifyX509` の `ca_cert` 未指定分岐は `LoadSystemSSLRootCertificates(store)` を経由するが、実装は `src/ssl_verifier_stub.cpp` の暫定実装（現行 4 段ロード相当）に閉じ込められている。

本 issue で `src/ssl_verifier_ios.mm` を新規追加し、iOS ターゲットのビルド構成を「`src/ssl_verifier_ios.mm` の 1 ファイル単独」に切り替えて、`SSLVerifier::VerifyX509` を Security.framework に検証委譲する形に置き換える。

sumomo は iOS 非対応（`examples/sumomo/run.py` の `AVAILABLE_TARGETS` に `ios` が含まれない）。SDK 本体の `ios` ターゲットは WSS / TURN-TLS 検証を含むため、動作検証は sora-ios-sdk などの iOS 向けクライアントから間接的に行う。

### マージ順序制約

本 issue の PR は 0036〜0038 / 0040 と同順（親 0035 マージ後、0040 マージ前）。0040 は 0036〜0039 全てがマージされた後にマージされる。

## 対象ビルドターゲット

| `SORA_TARGET` | `SORA_TARGET_OS` |
|---|---|
| `ios` | `ios` |

`python3 run.py build ios` は Device / Simulator（arm64）両方のスライスを含む `.xcframework` をビルドする。本 issue で追加する実装はスライス非依存の Objective-C++ / Security.framework 呼び出しのみ。

## 想定する動作環境

- iOS 14 以降（webrtc-build の `IOS_DEPLOYMENT_TARGET=14.0` と一致）。Security.framework の `SecTrustEvaluateWithError` は iOS 12+ で利用可能なため、下限には制約を加えない
- iOS のシステム trust store に接続先サーバの信頼アンカー（例: Sora Labo の `ISRG Root X1`）が収録されていること。iOS 14 のシステム trust store は ISRG Root X1 を含む
- 上記を満たさない環境や、独自 CA を信頼させたい場合は `SoraSignalingConfig::ca_cert` への PEM 明示指定で対応する

## 設計方針

### 実装方式: 検証委譲型

`SSLVerifier::VerifyX509(X509* x509, STACK_OF(X509)* chain, const std::optional<std::string>& ca_cert)` を Objective-C++ で丸ごと別実装する。BoringSSL の `X509_STORE` は使わず、以下の流れで Security.framework に検証委譲する:

1. 引数の `x509` と `chain` を DER にエンコード（`i2d_X509`）し、`SecCertificateCreateWithData` で `SecCertificateRef` の配列に変換する
2. `SecPolicyCreateBasicX509()` で SSL policy を作る。hostname 検証は BoringSSL 側（TLS ハンドシェイクの内部処理）が担うため、Security.framework には X509 chain 検証のみを委譲する
3. `SecTrustCreateWithCertificates(cert_array, policy, &trust)` で `SecTrustRef` を生成する
4. `ca_cert` が指定されている場合、指定 PEM を `SecCertificateRef` に変換して `SecTrustSetAnchorCertificates` で trust に設定し、`SecTrustSetAnchorCertificatesOnly(trust, true)` を呼んで system CA を混ぜないようにする。`ca_cert` 未指定の場合はこの手順をスキップして system trust store を使う
5. `SecTrustEvaluateWithError(trust, &error)` で検証を実行。成功なら `true`、失敗なら `CFErrorCopyDescription` でエラー文字列を取り出して `RTC_LOG(LS_WARNING)` に出し `false` を返す

`SoraSignalingConfig::ca_cert` の契約（指定 PEM のみを使い、system CA と混ぜない）は `SecTrustSetAnchorCertificatesOnly(true)` によって維持される。

`SoraSignalingConfig::insecure == true` の分岐は呼び出し側（`src/websocket.cpp:184`、`src/rtc_ssl_verifier.cpp:55`）に据え置きで、`SSLVerifier::VerifyX509` は insecure 時には呼ばれない。

### 実装されない関数

iOS では `SSLVerifier::VerifyX509` を丸ごと差し替えるため、次の関数は iOS ターゲットでは定義されない（他 4 OS では共通の `src/ssl_verifier.cpp` に定義される）:

- `SSLVerifier::AddCert(const std::string&, X509_STORE*)`
- `SSLVerifier::LoadSystemSSLRootCertificates(X509_STORE*)`

いずれも SDK 内部の `SSLVerifier::VerifyX509` からのみ呼ばれ、iOS 実装の `SSLVerifier::VerifyX509` はこれらを呼ばないため、iOS ターゲットで未解決シンボルにはならない。header の宣言は残る（他 4 OS が必要とするため）が、iOS では対応する定義はリンクされない。

### 実装骨格

```objectivec++
// src/ssl_verifier_ios.mm
#include "sora/ssl_verifier.h"

#include <functional>
#include <utility>
#include <vector>

#import <CoreFoundation/CoreFoundation.h>
#import <Security/Security.h>

// OpenSSL
#include <openssl/bio.h>
#include <openssl/err.h>
#include <openssl/pem.h>
#include <openssl/x509.h>

// WebRTC
#include <rtc_base/logging.h>

namespace sora {
namespace {

struct Guard {
  std::function<void()> f;
  Guard(std::function<void()> f) : f(std::move(f)) {}
  ~Guard() { f(); }
};

// X509 を DER にエンコードして SecCertificateRef を作る。失敗時は nullptr
SecCertificateRef CreateSecCertificate(X509* cert) {
  unsigned char* der = nullptr;
  int len = i2d_X509(cert, &der);
  if (len <= 0 || der == nullptr) {
    return nullptr;
  }
  CFDataRef data = CFDataCreate(nullptr, der, len);
  OPENSSL_free(der);
  if (data == nullptr) {
    return nullptr;
  }
  SecCertificateRef sec_cert = SecCertificateCreateWithData(nullptr, data);
  CFRelease(data);
  return sec_cert;
}

// ca_cert の PEM 文字列から SecCertificateRef の配列を作る。
// PEM は複数の CERTIFICATE ブロックを含みうる。1 件も取れなければ空
std::vector<SecCertificateRef> LoadCACertsFromPem(const std::string& pem) {
  std::vector<SecCertificateRef> result;
  BIO* bio = BIO_new_mem_buf(pem.c_str(), pem.size());
  if (bio == nullptr) {
    return result;
  }
  Guard bio_guard([bio]() { BIO_free(bio); });

  while (true) {
    X509* cert = PEM_read_bio_X509(bio, nullptr, nullptr, nullptr);
    if (cert == nullptr) {
      ERR_get_error();
      break;
    }
    SecCertificateRef sec_cert = CreateSecCertificate(cert);
    X509_free(cert);
    if (sec_cert == nullptr) {
      RTC_LOG(LS_WARNING)
          << "VerifyX509: failed to convert ca_cert PEM entry to SecCertificateRef";
      continue;
    }
    result.push_back(sec_cert);
  }
  return result;
}

}  // namespace

bool SSLVerifier::VerifyX509(X509* x509,
                             STACK_OF(X509) * chain,
                             const std::optional<std::string>& ca_cert) {
  // 診断ログ（他 4 OS の src/ssl_verifier.cpp:152-171 と同型）
  {
    char data[256];
    RTC_LOG(LS_INFO) << "cert:";
    X509_NAME_oneline(X509_get_subject_name(x509), data, sizeof(data));
    RTC_LOG(LS_INFO) << "  subject = " << data;
    X509_NAME_oneline(X509_get_issuer_name(x509), data, sizeof(data));
    RTC_LOG(LS_INFO) << "  issuer  = " << data;
    if (chain != nullptr) {
      int n = sk_X509_num(chain);
      for (int i = 0; i < n; i++) {
        X509* x = sk_X509_value(chain, i);
        RTC_LOG(LS_INFO) << "chain[" << i << "]:";
        X509_NAME_oneline(X509_get_subject_name(x), data, sizeof(data));
        RTC_LOG(LS_INFO) << "  subject = " << data;
        X509_NAME_oneline(X509_get_issuer_name(x), data, sizeof(data));
        RTC_LOG(LS_INFO) << "  issuer  = " << data;
      }
    }
  }

  // 引数の X509 と chain を SecCertificateRef の配列に変換
  std::vector<SecCertificateRef> sec_certs;
  Guard certs_guard([&sec_certs]() {
    for (SecCertificateRef c : sec_certs) {
      CFRelease(c);
    }
  });

  SecCertificateRef leaf = CreateSecCertificate(x509);
  if (leaf == nullptr) {
    RTC_LOG(LS_ERROR) << "VerifyX509: failed to convert leaf certificate";
    return false;
  }
  sec_certs.push_back(leaf);

  if (chain != nullptr) {
    int n = sk_X509_num(chain);
    for (int i = 0; i < n; i++) {
      SecCertificateRef mid = CreateSecCertificate(sk_X509_value(chain, i));
      if (mid == nullptr) {
        // 中間証明書 1 個の変換失敗は続行。chain が構築できなければ最終的に SecTrust 側で失敗する
        RTC_LOG(LS_WARNING)
            << "VerifyX509: failed to convert intermediate certificate: index=" << i;
        continue;
      }
      sec_certs.push_back(mid);
    }
  }

  CFArrayRef cert_array = CFArrayCreate(
      nullptr, reinterpret_cast<const void**>(sec_certs.data()),
      sec_certs.size(), &kCFTypeArrayCallBacks);
  if (cert_array == nullptr) {
    RTC_LOG(LS_ERROR) << "VerifyX509: CFArrayCreate failed";
    return false;
  }
  Guard cert_array_guard([cert_array]() { CFRelease(cert_array); });

  // hostname 検証は BoringSSL 側で行うため、ここでは basic X509 policy のみ
  SecPolicyRef policy = SecPolicyCreateBasicX509();
  if (policy == nullptr) {
    RTC_LOG(LS_ERROR) << "VerifyX509: SecPolicyCreateBasicX509 failed";
    return false;
  }
  Guard policy_guard([policy]() { CFRelease(policy); });

  SecTrustRef trust = nullptr;
  OSStatus status =
      SecTrustCreateWithCertificates(cert_array, policy, &trust);
  if (status != errSecSuccess || trust == nullptr) {
    RTC_LOG(LS_ERROR)
        << "VerifyX509: SecTrustCreateWithCertificates failed: status=" << status;
    return false;
  }
  Guard trust_guard([trust]() { CFRelease(trust); });

  if (ca_cert) {
    // ca_cert 指定時: 指定 PEM のみを anchor にし、system CA と混ぜない
    std::vector<SecCertificateRef> anchors = LoadCACertsFromPem(*ca_cert);
    Guard anchors_guard([&anchors]() {
      for (SecCertificateRef c : anchors) {
        CFRelease(c);
      }
    });
    if (anchors.empty()) {
      RTC_LOG(LS_ERROR)
          << "VerifyX509: no anchors loaded from ca_cert: ca_cert_length="
          << ca_cert->size();
      return false;
    }
    CFArrayRef anchor_array = CFArrayCreate(
        nullptr, reinterpret_cast<const void**>(anchors.data()),
        anchors.size(), &kCFTypeArrayCallBacks);
    if (anchor_array == nullptr) {
      RTC_LOG(LS_ERROR) << "VerifyX509: CFArrayCreate for anchors failed";
      return false;
    }
    Guard anchor_array_guard([anchor_array]() { CFRelease(anchor_array); });

    status = SecTrustSetAnchorCertificates(trust, anchor_array);
    if (status != errSecSuccess) {
      RTC_LOG(LS_ERROR)
          << "VerifyX509: SecTrustSetAnchorCertificates failed: status=" << status;
      return false;
    }
    status = SecTrustSetAnchorCertificatesOnly(trust, true);
    if (status != errSecSuccess) {
      RTC_LOG(LS_ERROR)
          << "VerifyX509: SecTrustSetAnchorCertificatesOnly failed: status="
          << status;
      return false;
    }
  }
  // ca_cert 未指定時: SecTrust は system trust store をそのまま使う

  CFErrorRef error = nullptr;
  bool verified = SecTrustEvaluateWithError(trust, &error);
  if (!verified) {
    if (error != nullptr) {
      CFStringRef desc = CFErrorCopyDescription(error);
      char buf[512] = {0};
      if (desc != nullptr) {
        CFStringGetCString(desc, buf, sizeof(buf), kCFStringEncodingUTF8);
        CFRelease(desc);
      }
      RTC_LOG(LS_WARNING)
          << "VerifyX509: SecTrustEvaluateWithError failed: " << buf;
      CFRelease(error);
    } else {
      RTC_LOG(LS_WARNING)
          << "VerifyX509: SecTrustEvaluateWithError failed: no error info";
    }
    return false;
  }
  RTC_LOG(LS_INFO) << "VerifyX509: SecTrustEvaluateWithError succeeded";
  return true;
}

}  // namespace sora
```

### CMakeLists.txt の変更

親 0035 が用意した分岐骨格の iOS 分岐で、`src/ssl_verifier.cpp` を含む `SORA_SSL_VERIFIER_SOURCES` を「`src/ssl_verifier_ios.mm` の 1 ファイルのみ」に書き換える。

```cmake
# 変更前（親 0035 の骨格）:
elseif (SORA_TARGET_OS STREQUAL "ios")
  set(SORA_SSL_VERIFIER_SOURCES src/ssl_verifier.cpp src/ssl_verifier_stub.cpp)

# 変更後:
elseif (SORA_TARGET_OS STREQUAL "ios")
  set(SORA_SSL_VERIFIER_SOURCES src/ssl_verifier_ios.mm)
```

iOS プラットフォーム分岐の既存 `target_link_libraries` に `-framework Security` と `-framework CoreFoundation` を追加する（CoreFoundation は WebRTC 経由でリンクされる可能性が高いが、Security.framework を直接使うため明示的にリンクしておく）。

Objective-C++ ファイルなので `.mm` 拡張子を選ぶ（`.cpp` では `#import` や Objective-C 型が使えない）。iOS プラットフォーム分岐の既存 `target_sources()`（Simulator / Device の両方をビルドする xcframework 生成の設定）は変更しない。

### スレッド安全性

`SSLVerifier::VerifyX509` は複数スレッド（Signaling スレッド、ICE スレッド）から並列に呼ばれ得る。実装内では次のリソースを使うが、いずれもスレッドセーフ:

- `SecCertificateCreateWithData` / `SecPolicyCreateBasicX509` / `SecTrustCreateWithCertificates` / `SecTrustSetAnchorCertificates` / `SecTrustSetAnchorCertificatesOnly` / `SecTrustEvaluateWithError`: Apple のドキュメントで明示的にスレッドセーフ（各呼び出しが独立の `SecTrustRef` を扱う限り）
- `i2d_X509` / `PEM_read_bio_X509` / `BIO_new_mem_buf`: BoringSSL の慣行上、独立のリソースを扱う限りスレッドセーフ
- 引数の `X509* x509` と `STACK_OF(X509)* chain`: 呼び出し側で並列共有されない前提（`RTCSSLVerifier` は呼び出しごとに独立のオブジェクトを渡す）

`SecTrustEvaluateWithError` は OCSP / CRL / CT の revocation チェックのためにネットワーク I/O を伴う可能性があり、ブロッキングになる。呼び出し元スレッドがネットワーク I/O 前提でないと想定外の遅延が起き得るが、これは他 4 OS の `X509_STORE_add_cert` を経た `X509_verify_cert` でも同じ性質（`X509_STORE_set_default_paths` 経由の OCSP responder 参照は起きうる）で、iOS だけの特殊事情ではない。

## 完了条件

- `src/ssl_verifier_ios.mm` が新規追加され、`SSLVerifier::VerifyX509(X509*, STACK_OF(X509)*, const std::optional<std::string>&)` を Security.framework の `SecTrustEvaluateWithError` に検証委譲する形で実装している
- `CMakeLists.txt` の親 0035 分岐骨格の iOS 分岐が `set(SORA_SSL_VERIFIER_SOURCES src/ssl_verifier_ios.mm)` に書き換わっている（`src/ssl_verifier.cpp` も `src/ssl_verifier_stub.cpp` も iOS では target_sources に含まれない）
- iOS プラットフォーム分岐の `target_link_libraries` に `-framework Security` と `-framework CoreFoundation` が追加されている
- iOS 実装は `ca_cert` 未指定時に system trust store を使い、指定時は `SecTrustSetAnchorCertificates` + `SecTrustSetAnchorCertificatesOnly(true)` で指定 PEM のみを trusted anchor にする
- iOS 実装は `SSLVerifier::AddCert` / `SSLVerifier::LoadSystemSSLRootCertificates` を呼ばない（両関数は iOS では未定義）
- 検証成功時に `RTC_LOG(LS_INFO) << "VerifyX509: SecTrustEvaluateWithError succeeded"` が出力される
- 検証失敗時に `RTC_LOG(LS_WARNING)` に `CFErrorCopyDescription` の内容を含めて出力される
- `python3 run.py build ios` が Device / Simulator の両方で通ること
- `python3 run.py build ubuntu-24.04_x86_64` / `python3 run.py build macos_arm64` / `python3 run.py build windows_x86_64` / `python3 run.py build android` は本 issue の変更対象外だが、GitHub Actions の CI 実行 URL を PR 本文に添付して他 OS の CMake 差分（iOS 分岐のみ）が他 OS を壊していないことを確認する
- テスト戦略節の全項目（ビルド確認・接続確認・回帰確認・証跡取得）を実施し、証跡ログを PR 本文に添付している
- `CHANGES.md` に `[CHANGE]` エントリを 1 本追加している。内容: (a) iOS の TLS 検証を iOS のシステム trust store（Apple 管理）に切り替えた旨、(b) 実装手段は Security.framework の `SecTrustEvaluateWithError` に検証委譲する形（sandbox 制約でアンカーの直接列挙が不可のため、他 4 OS の直接投入とは手段が異なる）である旨の補足、(c) 動作環境（iOS 14 以降、`IOS_DEPLOYMENT_TARGET=14.0` と一致）、(d) iOS のシステム trust store から Apple 公式の信頼判定（CT / revocation を含む）が反映される旨、(e) 独自 CA は `SoraSignalingConfig::ca_cert` で明示指定する旨

## 解決方法

`src/ssl_verifier/ssl_verifier_ios.mm` を新規追加し、`SSLVerifier::VerifyX509` を Security.framework の `SecTrustEvaluateWithError` に検証委譲する実装を行った。`CMakeLists.txt` で iOS ターゲットの `SORA_SYSTEM_CA_IMPL` を `src/ssl_verifier/ssl_verifier_stub.cpp` から `src/ssl_verifier/ssl_verifier_ios.mm` に切り替え、`-framework Security` と `-framework CoreFoundation` のリンクを追加した。`CHANGES.md` に `[CHANGE]` エントリを追加した。

テスト結果:
- ビルド確認: `python3 run.py build ios` 成功
- 接続確認: sora-ios-sdk のサンプルアプリを通じて Sora Labo 相当の公開 CA サーバーに WSS 接続成功
- 回帰確認: `e2e-test/` の E2E テスト PASS
- 証跡ログ: `VerifyX509: SecTrustEvaluateWithError succeeded` を確認

## 変更対象ファイル

- `src/ssl_verifier_ios.mm`（新規追加）
- `CMakeLists.txt`（iOS 分岐の `SORA_SSL_VERIFIER_SOURCES` 書き換え、iOS プラットフォーム分岐の `target_link_libraries` に `-framework Security` / `-framework CoreFoundation` 追加）
- `CHANGES.md`（`[CHANGE]` エントリ 1 本追加）

本 issue では `include/sora/ssl_verifier.h` / `src/ssl_verifier.cpp` / `src/ssl_verifier_stub.cpp` / `src/websocket.cpp` / `src/rtc_ssl_verifier.cpp` / `include/sora/rtc_ssl_verifier.h` は変更しない。

## テスト戦略

### ビルド確認

- 実装者ローカル: `python3 run.py build ios` が Device / Simulator 両方で通ることを確認する
- CI: GitHub Actions で他 OS ターゲット（Ubuntu / macOS / Windows / Android）のビルドが通ることを確認し、実行 URL を PR 本文に添付する（iOS 分岐外の差分は無いはずだが、`SORA_SSL_VERIFIER_SOURCES` 分岐骨格自体を触るためのセーフティネット）

### 接続確認

- sora-ios-sdk のサンプルアプリを通じて、Sora Labo 相当の公開 CA サーバー（Let's Encrypt 系、信頼アンカーは ISRG Root X1）に対して WSS で接続できることを確認する。実 CA / 実サーバーを使う（AGENTS.md「モックやスタブは絶対に利用しないこと」に従う）
- TURN-TLS 経路も同経由で通ることを確認する
- 使用する sora-ios-sdk のバージョン、サンプルアプリ名、動作確認した iOS バージョン / 実機 or Simulator は PR 本文に記載する

### 回帰確認

- `SoraSignalingConfig::ca_cert` 明示指定時と `insecure == true` の既存挙動が回帰していないことを、他 OS の E2E テスト（`e2e-test/` 配下）を CLAUDE.md 記載の形式（`uv run --directory=e2e-test pytest ... -v -s --timeout=60`）で回して確認する（0039 の CMake 変更が他 OS のコードパスを壊していないことの担保）。iOS ターゲットは `examples/sumomo/run.py` の `AVAILABLE_TARGETS` に含まれないため pytest 対象外。iOS の実機回帰は sora-ios-sdk 側での回帰確認手順を PR 本文に記載する

### 証跡取得（Security.framework 経路で検証が通ったことの実証）

- iOS 側で WebRTC の `RTC_LOG` レベルを `LS_INFO` 以上に設定して起動し、WSS 接続成功時に `VerifyX509: SecTrustEvaluateWithError succeeded` が Xcode / Console.app のログに出力されることを確認する（`RTC_LOG` は sora-ios-sdk 経由でロガーにブリッジされる）
- ログが出れば Security.framework 経路の `SecTrustEvaluateWithError` が成功した事実になる。Sora Labo の TLS 証明書は ISRG Root X1 に連なるため、他の信頼源がない状態で成功する事実がシステム trust store 経由の実証になる
- ログを PR 本文に添付する
- 失敗ケース（`SecTrustEvaluateWithError failed:` の WARNING）の実証は行わず、成功時ログを証跡とする方針を採る（0037 / 0038 / 0040 と同じ整理）

## 関連

- 親: `issues/0035-change-tls-trust-store-system-ca.md`
- 兄弟: `issues/0036-add-system-ca-store-linux.md` / `issues/0037-add-system-ca-store-macos.md` / `issues/0038-add-system-ca-store-windows.md` / `issues/0040-add-system-ca-store-android.md`
