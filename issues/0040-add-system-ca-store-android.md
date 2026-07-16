# Android でシステム CA を TLS 検証の信頼ストアに使う

- Priority: Medium
- Created: 2026-07-16
- Completed: {YYYY-MM-DD}
- Model: Composer 2.5
- Branch: feature/add-system-ca-store-android
- Polished: 2026-07-16

## 目的

Android 上の WSS / TURN-TLS 証明書検証で、端末のシステム CA ストアを信頼アンカーとして使う。親 issue 0035 の Android 実装。

## 優先度根拠

Medium。親 0035 と同じ。Android のシステム CA は `/system/etc/security/cacerts/` や `/apex/com.android.conscrypt/cacerts/` などにあり、BoringSSL 既定の `/etc/ssl/...` とは一致しない。現行は埋め込みルート頼みになっており、企業配布 CA を扱う運用ではシステム CA 対応の効果が大きい。

## 現状

親 0035 の PR がマージされた後、Android ターゲットは共通差し込み口 `SSLVerifier::LoadSystemSSLRootCertificates(X509_STORE*)` を経由するが、実装は `src/ssl_verifier_stub.cpp` の暫定実装に閉じ込められている。

本 issue で `src/ssl_verifier_android.cpp` を追加し、Android ターゲットの `LoadSystemSSLRootCertificates` を Android のシステム CA ディレクトリ読み込みに置き換える。

sumomo は Android 非対応（`examples/sumomo/run.py` の `AVAILABLE_TARGETS` に `android` が含まれない）。SDK 本体の `android` ターゲットは WSS / TURN-TLS 検証を含むため、動作検証は sora-android-sdk などの Android 向けクライアントから間接的に行う。

### マージ順序制約

本 issue の PR は 0036〜0039 が全て develop にマージされたのちにマージする（親 0035 の必須制約）。0040 が単独で先行すると、まだ実装されていない他 OS の子 PR がベースを更新したときに `ssl_verifier_stub.cpp` の参照を失いビルド不能になる。

## 対象ビルドターゲット

| `SORA_TARGET` | `SORA_TARGET_OS` |
|---|---|
| `android` | `android` |

`python3 run.py build android` は `arm64-v8a` の単一 ABI をビルドする（`run.py:187` で `-DANDROID_ABI=arm64-v8a` を決め打ち。他 ABI の切り替えは現時点で用意されていない）。

## 想定する動作環境

- Android 10 以降（`DEPS` の `ANDROID_NATIVE_API_LEVEL=29` と一致する）
- システム CA ストアに ISRG Root X1 が収録されていること（Android 10 以降で収録済み）
- 本実装は次の 2 つのディレクトリを取得元にする:
  - `/apex/com.android.conscrypt/cacerts/`（Android 14 以降で Conscrypt Mainline module 経由の更新可能な CA ストアとして提供される。Android 10-13 では存在しない）
  - `/system/etc/security/cacerts/`（AOSP 標準の system パス。Android 10-13 の主要な CA ストアであり、Android 14 以降でも互換のために残る。factory image 時点で凍結され Play system update では更新されない）
- Android の `KeyChain` API 経由の Trusted credentials（`/data/misc/user/0/cacerts-added/` などにユーザや管理者が追加した独自 CA。バージョンによりパスは異なる）や、アプリの Network Security Config で `<trust-anchors>` に指定されたリソースは本実装では反映されない
- 上記を満たさない環境や、独自 CA を信頼させたい場合は `SoraSignalingConfig::ca_cert` への PEM 明示指定で対応する（他 OS の子 issue と同じ方針）

## 設計方針

### 信頼アンカーの取得元と読み取り順序

上記の 2 ディレクトリを、次の順で読む:

1. `/apex/com.android.conscrypt/cacerts/`（Android 14 以降）
2. `/system/etc/security/cacerts/`（AOSP 標準）

BoringSSL の `X509_STORE_add_cert` は同一 subject の CA を「重複」として扱う（BoringSSL の実装バージョンにより、`X509_R_CERT_ALREADY_IN_HASH_TABLE` を積んで return 0 する版と、silently ignore で return 1 する版がある。いずれの版でも「先に投入された CA が採用され、後続の同 subject は無視される」意味論は共通）。同一 subject の CA が両方に存在する場合、先に `X509_STORE_add_cert` を通したディレクトリの証明書が採用される。

Conscrypt Mainline module は Google Play system update で CA 束を配信して端末外から更新可能にする仕組みであり、apex 側が新しい版を持ち得るため、apex を優先する順序が Conscrypt Mainline の設計意図と一致する。system 側は factory image 時点で凍結されるため、apex 側の更新済み CA が採用されるよう「apex 先」を選ぶ。

Android 10-13 端末では `/apex/com.android.conscrypt/cacerts/` は存在せず `opendir` が `ENOENT` を返すため、実質 `/system/etc/security/cacerts/` のみが取得元となる。

### 実装方式の選定

C API のみで完結するため実装ファイルを `.cpp` にできる（JNI や Java ランタイム不要）。JNI 経由（`X509TrustManager.getAcceptedIssuers()` 等）は既存の `src/java_context.cpp` の `sora::GetJNIEnv()` を使えば技術的には可能だが、次の理由で却下する:

- `LoadSystemSSLRootCertificates` は毎ハンドシェイクで呼ばれるため、JNI 呼び出しのコスト（Java オブジェクト生成、`JNIEnv->AttachCurrentThread` の管理）がハンドシェイク遅延に上乗せされる
- `sora::GetJNIEnv()` の生存は Android アプリのライフサイクルに依存し、TURN-TLS 再接続などのタイミング次第で JVM が使えない状態を考慮する分岐が必要になる
- ファイル直読みなら Java ランタイム状態を意識しない C++ 単独の実装に閉じる

Network Security Config の per-host ルールや、`android.security.KeyChain` API 経由の Trusted credentials（`/data/misc/user/0/cacerts-added/` 等）を本実装が反映しないのは、これらを反映するには `X509_STORE` へのアンカー列挙契約（`bool(X509_STORE*)`）だけでは表現不可能な per-host / per-app ルールを持ち込む必要があるため。反映が必要な利用者は `SoraSignalingConfig::ca_cert` に PEM を明示指定する。

各ファイルは AOSP の CA 生成過程で c_rehash された `<subject_hash>.<n>` 命名の PEM 形式（例: `.0` 拡張子）で、先頭に `-----BEGIN CERTIFICATE-----` ブロックを持つ。後続に人間可読な issuer/subject/fingerprint や trust extension のヘキサダンプが続くが、`PEM_read_bio_X509` はそれらを読み飛ばして CERTIFICATE ブロックのみを X509 として取り出す。

`X509_STORE_set_default_paths` は本実装では呼ばない（環境変数 `SSL_CERT_FILE` / `SSL_CERT_DIR` の注入経路を残さないセキュリティ配慮。0036 / 0037 / 0038 / 0039 と同じ方針）。

### 実装骨格

NDK の `opendir` / `readdir` でディレクトリを走査し、各ファイルを `BIO_new_file` + `PEM_read_bio_X509` で読んで `X509_STORE_add_cert` する。`DIR*` と `BIO*` の解放漏れを防ぐため、Guard パターン（親 0035 マージ後の `src/ssl_verifier.cpp` の `SSLVerifier::VerifyX509` 内で定義されている `Guard` 構造体と同型。TU ローカルに独立コピーを持ち、共通ヘッダには切り出さない）で `closedir` / `BIO_free` を保証する。`X509` は `X509_STORE_add_cert` 成功・失敗どちらの分岐でも直後に `X509_free` する。

`opendir` 失敗時は `errno == ENOENT`（Android 10-13 で `/apex/` パスが存在しない期待ケース）は無音で 0 件を返し続行、それ以外（`EACCES` / `EMFILE` / `ENOTDIR` 等）は `RTC_LOG(LS_WARNING)` に errno を付けて出して 0 件を返す。SELinux 変更でアプリドメインから読めなくなった端末や FD 枯渇を運用ログから追跡できるようにする。

`X509_STORE_add_cert` 失敗時は現行の全 OS 実装（親 0035 マージ後の `SSLVerifier::LoadBuiltinSSLRootCertificates` の失敗続行分岐と同型）と同じく `RTC_LOG(LS_WARNING)` を出して続行する。`SSLVerifier::AddCert` は失敗即 `false` を返し親 0035 の「部分失敗は続行、0 件で `false`」契約と衝突するため使わない。失敗時ログには `X509_NAME_oneline(X509_get_subject_name(cert), ...)` で subject 名と `entry->d_name` を含めて原因追跡できるようにする。

BoringSSL のバージョンによっては、同一 CA が両ディレクトリに存在した場合、後に読む system 側は `X509_R_CERT_ALREADY_IN_HASH_TABLE` を積んで `X509_STORE_add_cert` が 0 を返す（Windows 0038 と同じ扱い）。Android 14+ で同 subject の CA が多数含まれる場合これが多発するため、`X509_STORE_add_cert` 失敗直後に `ERR_peek_last_error()` で reason を検査し、`X509_R_CERT_ALREADY_IN_HASH_TABLE` の場合のみエラーキューをクリアだけ行い WARNING は出さない（他 reason の失敗はログを残す）。silently ignore する版の BoringSSL では 0 を返さないため、この分岐には入らない（無害）。

`PEM_read_bio_X509` が `nullptr` を返した際はエラーキューから 1 件取り出してループを抜ける（現行の全 OS 実装と同型）。`BIO_new_file` が `nullptr` を返した際も同様にエラーキューから 1 件取り出して次イテレーションのエラー報告を汚染しないようにする。

両ディレクトリを試し終えた時点で、合計で 1 件も追加できなかった場合は `RTC_LOG(LS_ERROR)` で英語 1 行（内訳付き: `added=0 apex=<L> system=<M>`）を出し `false` を返す。1 件以上追加できていれば、成功時ログを出して `true` を返す。

関数が `true` を返す経路の return 直前に `RTC_LOG(LS_INFO) << "LoadSystemSSLRootCertificates: added=<N>, apex=<L>, system=<M>"` を 1 回出す。`added` は 2 ディレクトリでの `X509_STORE_add_cert` 成功件数の合計（重複拒否した版の BoringSSL では apex の全件数 + system 側の apex に含まれなかった差分件数、silently ignore する版では apex 全件 + system 全件）で、ディレクトリ別の内訳付きで Android バージョン依存のトラブル分析に使える運用証跡になる。

```cpp
// src/ssl_verifier_android.cpp
#include "sora/ssl_verifier.h"

#include <dirent.h>
#include <errno.h>

#include <functional>
#include <string>
#include <utility>

#include <openssl/bio.h>
#include <openssl/err.h>
#include <openssl/pem.h>
#include <openssl/x509.h>

#include <rtc_base/logging.h>

namespace sora {
namespace {

// 単一ディレクトリを走査してストアに追加、追加件数を返す。
// opendir 失敗時は errno == ENOENT なら無音で 0（Android バージョンで片方の経路が無いケース）、
// それ以外は WARNING を出して 0 を返す
int LoadFromDir(X509_STORE* store, const char* dir_path) {
  struct Guard {
    std::function<void()> f;
    Guard(std::function<void()> f) : f(std::move(f)) {}
    ~Guard() { f(); }
  };

  DIR* dir = opendir(dir_path);
  if (dir == nullptr) {
    int e = errno;
    if (e != ENOENT) {
      RTC_LOG(LS_WARNING)
          << "LoadSystemSSLRootCertificates: opendir failed: path=" << dir_path
          << " errno=" << e;
    }
    return 0;
  }
  Guard dir_guard([dir]() { closedir(dir); });

  int added = 0;
  struct dirent* entry;
  while ((entry = readdir(dir)) != nullptr) {
    if (entry->d_name[0] == '.') {
      // AOSP CA ファイルは <subject_hash>.<n> 命名でドット始まりを含まないため
      // "." / ".." およびドット始まりの隠しファイルは対象外で安全
      continue;
    }
    std::string path = std::string(dir_path) + "/" + entry->d_name;
    BIO* bio = BIO_new_file(path.c_str(), "r");
    if (bio == nullptr) {
      ERR_get_error();
      RTC_LOG(LS_WARNING)
          << "LoadSystemSSLRootCertificates: BIO_new_file failed: path=" << path;
      continue;
    }
    Guard bio_guard([bio]() { BIO_free(bio); });

    while (true) {
      X509* cert = PEM_read_bio_X509(bio, nullptr, nullptr, nullptr);
      if (cert == nullptr) {
        ERR_get_error();
        break;
      }
      int r = X509_STORE_add_cert(store, cert);
      if (r == 0) {
        // 重複拒否する版の BoringSSL では X509_R_CERT_ALREADY_IN_HASH_TABLE が積まれる。
        // この場合はエラーキューから 1 件取り出すのみで WARNING は出さない。
        // 他 reason（allocation 失敗等）は WARNING を出す
        unsigned long err = ERR_peek_last_error();
        if (ERR_GET_REASON(err) == X509_R_CERT_ALREADY_IN_HASH_TABLE) {
          ERR_get_error();
        } else {
          char subject[256] = {0};
          X509_NAME_oneline(X509_get_subject_name(cert), subject, sizeof(subject));
          RTC_LOG(LS_WARNING)
              << "LoadSystemSSLRootCertificates: X509_STORE_add_cert failed: file="
              << entry->d_name << " subject=" << subject;
          ERR_get_error();
        }
      } else {
        ++added;
      }
      X509_free(cert);
    }
  }

  return added;
}

}  // namespace

bool SSLVerifier::LoadSystemSSLRootCertificates(X509_STORE* store) {
  // Conscrypt Mainline module 経由の更新可能な CA ストア（Android 14 以降で提供）を優先
  int added_apex = LoadFromDir(store, "/apex/com.android.conscrypt/cacerts");
  // AOSP 標準の system パス（Android 10-13 の主要ストア、Android 14+ でも残る）
  int added_system = LoadFromDir(store, "/system/etc/security/cacerts");
  int added = added_apex + added_system;

  if (added == 0) {
    RTC_LOG(LS_ERROR)
        << "LoadSystemSSLRootCertificates: no certificates loaded: added=0"
        << " apex=" << added_apex << " system=" << added_system;
    return false;
  }
  RTC_LOG(LS_INFO)
      << "LoadSystemSSLRootCertificates: added=" << added
      << ", apex=" << added_apex << ", system=" << added_system;
  return true;
}

}  // namespace sora
```

### CMakeLists.txt の変更

親 0035 が用意した共通差し込み口の切り替え分岐（`SORA_SSL_VERIFIER_SOURCES` 変数と、直後の無条件 `target_sources(sora PRIVATE ${SORA_SSL_VERIFIER_SOURCES})`）を、次の形に置き換える。iOS は `src/ssl_verifier_ios.mm` の 1 ファイル単独、他 4 OS は `src/ssl_verifier.cpp` + OS 別ソースの 2 ファイル:

```cmake
if (SORA_TARGET_OS STREQUAL "ubuntu")
  target_sources(sora PRIVATE src/ssl_verifier.cpp src/ssl_verifier_ubuntu.cpp)
elseif (SORA_TARGET_OS STREQUAL "macos")
  target_sources(sora PRIVATE src/ssl_verifier.cpp src/ssl_verifier_macos.cpp)
elseif (SORA_TARGET_OS STREQUAL "windows")
  target_sources(sora PRIVATE src/ssl_verifier.cpp src/ssl_verifier_windows.cpp)
elseif (SORA_TARGET_OS STREQUAL "ios")
  target_sources(sora PRIVATE src/ssl_verifier_ios.mm)
elseif (SORA_TARGET_OS STREQUAL "android")
  target_sources(sora PRIVATE src/ssl_verifier.cpp src/ssl_verifier_android.cpp)
else ()
  message(FATAL_ERROR "Unknown SORA_TARGET_OS: ${SORA_TARGET_OS}")
endif ()
```

親 0035 の骨格が保っていた `SORA_TARGET_OS` に応じた排他選択と、未知 OS 検知の `FATAL_ERROR` ガードは維持する。`SORA_SSL_VERIFIER_SOURCES` 変数と後続の `target_sources(sora PRIVATE ${SORA_SSL_VERIFIER_SOURCES})` の 2 行は不要になるため削除する。各 OS プラットフォーム分岐（Windows / macOS / iOS / Android / Ubuntu）の内部には手を入れない（iOS の `-framework Security` / `-framework CoreFoundation` は 0039 で追加済み）。

既存の Android プラットフォーム分岐に対する追加リンクは不要（`opendir` / `readdir` / `BIO_*` / `PEM_*` / `X509_*` は bionic libc と BoringSSL で解決される。BoringSSL は WebRTC 内部にリンクされており、`find_package(WebRTC REQUIRED)` を経由して sora の全 OS ターゲットで既にリンク済み）。

### スレッド安全性

`VerifyX509`（親 0035 マージ後の `src/ssl_verifier.cpp` の `SSLVerifier::VerifyX509`）は毎回 `X509_STORE_new()` で新規ストアを作り、これに対して `LoadSystemSSLRootCertificates` を呼ぶ。1 スレッドが 1 ストアを扱う関係のため、`X509_STORE_add_cert` の並列競合は発生しない。`readdir` は同一 `DIR*` を複数スレッドで共有すると POSIX 上未定義だが、本実装は 1 スレッドが 1 個の `DIR*` を独占するため安全。`BIO_new_file` / `PEM_read_bio_X509` は独立のリソースを扱う BoringSSL 慣行上スレッドセーフ。CA ディレクトリは read-only オープンのみでファイル I/O 競合も発生しない。

### 実行時性能

Android 14+ 端末では両ディレクトリで概ね 130 件前後（apex/system それぞれ、多くが重複）を毎ハンドシェイクで走査する。ハンドシェイクごとのファイル open + PEM parse コストが数〜十数 ms オーダーで乗る想定。親 0035 の「毎回列挙、キャッシュなし」契約に従うためこのコストは受け入れる。将来キャッシュ導入の余地はあるが本 issue のスコープ外。

## 完了条件

- `src/ssl_verifier_android.cpp` が新規追加され、`SSLVerifier::LoadSystemSSLRootCertificates(X509_STORE*)` の Android 実装を `SSLVerifier::` メンバ関数として保持している
- `CMakeLists.txt` から親 0035 が用意した `SORA_SSL_VERIFIER_SOURCES` 変数と後続の無条件 `target_sources(sora PRIVATE ${SORA_SSL_VERIFIER_SOURCES})` が削除され、代わりに `if (SORA_TARGET_OS STREQUAL "ubuntu") ... elseif ... elseif ... else () message(FATAL_ERROR ...) endif ()` 分岐内で各 OS 別ソースが `target_sources(sora PRIVATE ...)` で直接指定されている（他 4 OS は `src/ssl_verifier.cpp` + `src/ssl_verifier_<os>.cpp` の 2 ファイル、iOS は `src/ssl_verifier_ios.mm` の 1 ファイル）
- `src/ssl_verifier_stub.cpp` が削除されている
- `src/sora_signaling.cpp` の該当箇所コメント（親 PR 時点で「rtc_base/ssl_roots.h には Let's Encrypt が無い」旨が残っている）がシステム CA 方針に合わせて更新されている
- 親 0035 の `LoadSystemSSLRootCertificates` 契約を満たしている（1 件以上追加で `true` / 部分失敗は `RTC_LOG(LS_WARNING)` 続行 / 0 件は `RTC_LOG(LS_ERROR)` で `false` / キャッシュなし / スレッドセーフ）
- Android 実装は `/apex/com.android.conscrypt/cacerts/` と `/system/etc/security/cacerts/` の 2 ディレクトリのみを取得元とし、apex を先に読む順序で実装されている
- Android 実装で `X509_STORE_set_default_paths` を呼んでいない
- Android 実装は `SSLVerifier::AddCert` を呼ばない
- 関数が `true` を返す経路の return 直前に `RTC_LOG(LS_INFO) << "LoadSystemSSLRootCertificates: added=<N>, apex=<L>, system=<M>"` が 1 回出力される
- 0 件失敗時の `RTC_LOG(LS_ERROR)` にも apex / system の内訳が含まれる
- `X509_R_CERT_ALREADY_IN_HASH_TABLE` による重複拒否では `RTC_LOG(LS_WARNING)` を出さず、それ以外の `X509_STORE_add_cert` 失敗のみ WARNING を出す
- `python3 run.py build android` が通ること
- 0040 の CMake 変更が他 OS のビルドを壊していないことを、GitHub Actions の CI（他 OS ターゲットを含む run.py build ワークフロー）で確認し、実行 URL を PR 本文に添付する。実装者ローカルでは Android と、実装者のホスト OS で扱えるターゲット（Mac ホストなら macos / iOS、Linux ホストなら ubuntu 系）でローカルビルドを追加確認する
- Android 10-13 と Android 14 以降の両方のバージョン（実機 or エミュレータ、片方または両方の混在は可）で接続確認と証跡取得を行い、証跡ログを PR 本文に添付している
- `CHANGES.md` に 2 本の `[CHANGE]` エントリを追加している。1 本目: (a) Android の TLS 検証を OS のシステム CA（`/apex/com.android.conscrypt/cacerts/` と `/system/etc/security/cacerts/`）に切り替えた旨、(b) 対応 Android バージョン（Android 10 以降、`ANDROID_NATIVE_API_LEVEL=29` と一致）、(c) `KeyChain` API 経由の Trusted credentials や Network Security Config は反映されない旨、(d) 独自 CA は `SoraSignalingConfig::ca_cert` で明示指定する旨。2 本目: `src/ssl_verifier_stub.cpp` の削除に伴い、旧ハードコード PEM（`isrg_root` / `lets_encrypt_r3`）や WebRTC `ssl_roots.h` 依存も完全に消え、TLS 検証の信頼ストア切り替えが全 OS で完了した旨

## 解決方法

1. `src/ssl_verifier_android.cpp` を新規追加し、上記の設計方針・実装骨格に従って `SSLVerifier::LoadSystemSSLRootCertificates(X509_STORE*)` を実装する
2. `CMakeLists.txt` の `SORA_SSL_VERIFIER_SOURCES` 変数と後続 `target_sources` を削除し、上記の「### CMakeLists.txt の変更」節のとおり `if / elseif / else` 分岐内で直接 `target_sources` を呼ぶ形に置き換える
3. `src/ssl_verifier_stub.cpp` を削除する
4. `src/sora_signaling.cpp` の該当コメントを更新する
5. テスト戦略節に従い、ビルド確認・接続確認・回帰確認・証跡取得を行う
6. `CHANGES.md` に 2 本の `[CHANGE]` エントリを追加する

## 変更対象ファイル

- `src/ssl_verifier_android.cpp`（新規追加）
- `src/ssl_verifier_stub.cpp`（削除）
- `src/sora_signaling.cpp`（該当コメントの更新）
- `CMakeLists.txt`（`SORA_SSL_VERIFIER_SOURCES` 変数と後続 `target_sources` の削除、if / elseif / else 分岐内での直接指定への置き換え）
- `CHANGES.md`（`[CHANGE]` エントリ 2 本追加）

本 issue では `include/sora/ssl_verifier.h` / `src/ssl_verifier.cpp` / `src/websocket.cpp` / `src/rtc_ssl_verifier.cpp` / `include/sora/rtc_ssl_verifier.h` は変更しない。

## テスト戦略

### ビルド確認

- 実装者ローカル: `python3 run.py build android` が通ることを確認する。加えて、実装者のホスト OS で扱える他ターゲット（Mac ホストなら `python3 run.py build macos_arm64` / `python3 run.py build ios`、Linux ホストなら `python3 run.py build ubuntu-24.04_x86_64`）でローカルビルドを追加確認する
- CI: GitHub Actions で他 OS のターゲット（Ubuntu / macOS / iOS / Windows）ビルドが通ることを確認し、実行 URL を PR 本文に添付する

### 接続確認

- sora-android-sdk のサンプルアプリを通じて、Sora Labo 相当の公開 CA サーバー（Let's Encrypt 系、信頼アンカーは ISRG Root X1）に対して WSS で接続できることを確認する。実 CA / 実サーバーを使う（AGENTS.md「モックやスタブは絶対に利用しないこと」に従う）
- TURN-TLS 経路も同経由で通ることを確認する
- Android 10-13 と Android 14 以降の両方のバージョンで実施する（実機・エミュレータの混在可）。使用する sora-android-sdk のバージョン、サンプルアプリ名、動作確認した Android バージョン / API level / 実機かエミュレータかは PR 本文に記載する

### 回帰確認

- `SoraSignalingConfig::ca_cert` 明示指定時と `insecure == true` の既存挙動が回帰していないことを、他 OS の E2E テスト（`e2e-test/` 配下）を CLAUDE.md 記載の形式（`uv run --directory=e2e-test pytest ... -v -s --timeout=60`）で回して確認する（0040 の CMake 変更が他 OS のコードパスを壊していないことの担保）。Android ターゲットは `examples/sumomo/run.py` の `AVAILABLE_TARGETS` に含まれないため pytest 対象外。Android の実機回帰は sora-android-sdk 側での回帰確認手順を PR 本文に記載する

### 証跡取得（Android システム CA ディレクトリから読んだことの実証）

- Android 側で WebRTC の `RTC_LOG` レベルを `LS_INFO` 以上に設定して起動し、WSS 接続成功時に `LoadSystemSSLRootCertificates: added=<N>, apex=<L>, system=<M>` が `adb logcat` に出力されることを確認する（`RTC_LOG` は sora-android-sdk 経由で logcat にブリッジされる）
- N が 1 以上であれば、いずれかのシステム CA ディレクトリから少なくとも 1 件のアンカーを読み込み、それにより接続の chain building が成功したことになる（Sora Labo の TLS 証明書は ISRG Root X1 に連なるため、他の信頼源がない状態で成功する事実がシステム CA 経由の実証になる）
- Android バージョンごとの概ねの分布: Android 10-13 端末では `apex == 0, system > 0`、Android 14 以降端末では `apex > 0, system > 0`。ベンダー独自ビルドや部分的な CA 移行等で分布が異なる場合もあるため、実機検証で確認する
- Android 10-13 と Android 14 以降それぞれの `adb logcat` 該当ログを PR 本文に添付する
- Android では Linux の Docker 隔離のような「システム CA ディレクトリを一時的に無効化」する手段は限定的なため、失敗ケースの実証は行わず、成功時ログを証跡とする方針を採る（0037 / 0038 / 0039 と同じ整理）

## 関連

- 親: `issues/0035-change-tls-trust-store-system-ca.md`
- 兄弟: `issues/0036-add-system-ca-store-linux.md` / `issues/0037-add-system-ca-store-macos.md` / `issues/0038-add-system-ca-store-windows.md` / `issues/0039-add-system-ca-store-ios.md`
