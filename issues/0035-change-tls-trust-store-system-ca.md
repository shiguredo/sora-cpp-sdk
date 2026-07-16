# TLS 検証の信頼ストアをシステム CA に切り替える

- Priority: Medium
- Created: 2026-07-16
- Completed: {YYYY-MM-DD}
- Model: Composer 2.5
- Branch: feature/change-tls-trust-store-system-ca
- Polished: 2026-07-16

## 目的

WSS / TURN-TLS のサーバー証明書検証で、Let's Encrypt と WebRTC `ssl_roots.h` のハードコードされたルート証明書に依存するのをやめ、OS のシステム CA ストアを信頼の根拠にする。`SoraSignalingConfig::ca_cert` の契約（指定 PEM のみを使い、システム CA と混ぜない）は現状のまま維持する。

## 優先度根拠

Medium。公開 CA（Let's Encrypt 等）では現状でも接続できることが多いが、社内 CA・独自 CA・OS 側で管理される信頼設定を反映できない。macOS / Windows / iOS / Android では BoringSSL の `/etc/ssl/...` 既定パスが事実上効かず、埋め込みルート頼みになっている。`SSLVerifier::LoadBuiltinSSLRootCertificates` は `private static` メンバであり公開 API の削除は伴わないため High には引き上げない。

## 現状

WSS と TURN-TLS はともに最終的に `SSLVerifier::VerifyX509`（`src/ssl_verifier.cpp:149`）で検証する。

- WSS: `src/websocket.cpp:180-190` の verify callback。分岐上は `preverified == true` ならそのまま `true` を返す形だが、`CreateSSLContext`（`src/websocket.cpp:61-103`）は `SSL_CTX_new(::TLS_method())` に対し `set_default_verify_paths()`（`src/websocket.cpp:75` でコメントアウト済み）も `load_verify_file` も呼ばず、SSL_CTX に CA を一切ロードしない。従って現行では OpenSSL の事前検証は必ず失敗して `preverified == false` になり、TLS 検証は事実上すべて `SSLVerifier::VerifyX509` に流れる
- WebRTC 側の TLS 証明書検証全般（TURN-TLS を含む）: `RTCSSLVerifier`（`src/rtc_ssl_verifier.cpp:54`）→ `SSLVerifier::VerifyX509`。`RTCSSLVerifier` は `sora_signaling.cpp:600-602` で `dependencies.tls_cert_verifier` に登録される
- 設定は `SoraSignalingConfig::insecure`（`include/sora/sora_signaling.h:105`）と `SoraSignalingConfig::ca_cert`（同 163 行）
- 本 issue の変更範囲でも `src/websocket.cpp:75` のコメントアウトは維持する（SSL_CTX 側に CA をロードすると `SSLVerifier::VerifyX509` を経由しないパスができ、システム CA への切り替え効果が半分になる）

`ca_cert` 未指定かつ `insecure == false` のとき、`SSLVerifier::VerifyX509`（`src/ssl_verifier.cpp:199-212`）は次の順で信頼ストアを構築する。以降これを「現行ロード処理」と呼ぶ。

1. `AddCert(isrg_root, store)`：ハードコードされた ISRG Root X1 の PEM（`src/ssl_verifier.cpp:23-62` に定義）を `X509_STORE` に追加
2. `AddCert(lets_encrypt_r3, store)`：ハードコードされた Let's Encrypt R3 の PEM（同 64-97 行に定義）を `X509_STORE` に追加
3. `LoadBuiltinSSLRootCertificates(store)`（同 127-147 行）：WebRTC `rtc_base/ssl_roots.h` のルート証明書群を `X509_STORE` に追加
4. `X509_STORE_set_default_paths(store)`：BoringSSL の既定検索パス（コンパイル時デフォルトの `/etc/ssl/cert.pem` / `/etc/ssl/certs` および環境変数 `SSL_CERT_FILE` / `SSL_CERT_DIR`）を検索対象に登録

`ca_cert` 指定時は指定 PEM のみを使い、上記 1〜4 は行わない（この挙動は維持する）。

問題点:

- Keychain / Windows 証明書ストア / Android システム CA を読まない
- OS ごとに信頼できる CA 集合が埋め込みルートに引きずられる
- Let's Encrypt 中間証明書のハードコードはルート信頼の本来の役割ではない

## 対象ビルドターゲット

親 issue では全ターゲットを対象とし、OS 固有実装は各子 issue に閉じる。CMake の `SORA_TARGET_OS` 値と対応させる。実装手段の用語は次のとおり:

- **アンカー列挙型**: OS API でシステム CA を列挙し、`X509_STORE` に投入して BoringSSL の `X509_verify_cert` で検証する。他 4 OS はこれを採る
- **検証委譲型**: `SSLVerifier::VerifyX509` を丸ごと差し替え、OS 側の検証 API（iOS の `SecTrustEvaluateWithError`）に検証委譲する。iOS のみこれを採る

| 子 issue | `SORA_TARGET_OS` | 対象 `SORA_TARGET` | OS 別ソースファイル名 | 最終形のビルドファイル | 実装手段 |
|---|---|---|---|---|---|
| 0036 | `ubuntu` | `ubuntu-22.04_x86_64` / `ubuntu-24.04_x86_64` / `ubuntu-22.04_armv8` / `ubuntu-24.04_armv8` / `raspberry-pi-os_armv8` | `src/ssl_verifier_ubuntu.cpp` | `src/ssl_verifier.cpp` + `src/ssl_verifier_ubuntu.cpp` の 2 ファイル | アンカー列挙型 |
| 0037 | `macos` | `macos_arm64` | `src/ssl_verifier_macos.cpp` | `src/ssl_verifier.cpp` + `src/ssl_verifier_macos.cpp` の 2 ファイル | アンカー列挙型 |
| 0038 | `windows` | `windows_x86_64` | `src/ssl_verifier_windows.cpp` | `src/ssl_verifier.cpp` + `src/ssl_verifier_windows.cpp` の 2 ファイル | アンカー列挙型 |
| 0039 | `ios` | `ios`（`SORA_TARGET` は 1 値で Device / Simulator の両スライスを含む `.xcframework` を生成する） | `src/ssl_verifier_ios.mm` | `src/ssl_verifier_ios.mm` 単独（1 ファイル） | **検証委譲型** |
| 0040 | `android` | `android` | `src/ssl_verifier_android.cpp` | `src/ssl_verifier.cpp` + `src/ssl_verifier_android.cpp` の 2 ファイル | アンカー列挙型 |

信頼の根拠を各 OS のシステム CA ストアに置くという目的は全 5 子で共通。実装手段の詳細は表の「実装手段」列と、iOS 例外の理由は「iOS の実装手段」節を参照。

Raspberry Pi OS の `ubuntu` 集約は `CMakeLists.txt:58-61`。`macos_x86_64` は `run.py` の `AVAILABLE_TARGETS` に含まれず現行のビルド対象外（本 issue のスコープ外）。

## 想定する動作環境（システム CA の前提）

各 OS のシステム CA ストアに ISRG Root X1 が収録されていることを前提にする。実務上の下限:

- Ubuntu / Raspberry Pi OS: `ca-certificates` パッケージが導入され、`/etc/ssl/certs/ca-certificates.crt` を含む標準構成であること
- macOS: webrtc-build の `MACOS_DEPLOYMENT_TARGET=14` と一致する Sonoma (14.x) 以降。いずれも収録済み
- Windows: 現行サポート範囲（Windows 10 以降）で AuthRoot が更新されていれば収録済み
- iOS: webrtc-build の `IOS_DEPLOYMENT_TARGET=14.0` と一致する iOS 14 以降。iOS 14 のシステム trust store は ISRG Root X1 を含む
- Android: DEPS の `ANDROID_NATIVE_API_LEVEL=29`（Android 10）以降を対象とする。この API level のシステム CA ストアには ISRG Root X1 が含まれる

これらを満たさない環境では `SoraSignalingConfig::ca_cert` の明示指定で回避することを、各子 PR の `[CHANGE]` エントリの移行手段として記載する（記載方針は後述）。

## 設計方針

### iOS の実装手段

iOS は Apple のサンドボックス設計上、アプリから system trust store のアンカーを列挙する public API が存在しない（`SecTrustCopyAnchorCertificates` / `SecTrustSettingsCopyCertificates` はいずれも macOS 専用で iOS では利用不可）。そのため iOS だけは他 4 OS と実装手段を分ける:

- 他 4 OS: 共通の `src/ssl_verifier.cpp` に含まれる `SSLVerifier::VerifyX509` を使い、`ca_cert` 未指定分岐で `SSLVerifier::LoadSystemSSLRootCertificates(X509_STORE*)` を経由してシステム CA を `X509_STORE` に投入する
- iOS: 共通の `src/ssl_verifier.cpp` をビルドせず、`src/ssl_verifier_ios.mm` が `SSLVerifier::VerifyX509` の別実装を提供する。BoringSSL の X509 検証は使わず Security.framework の `SecTrustEvaluateWithError` に検証委譲する。`SSLVerifier::LoadSystemSSLRootCertificates` は iOS では定義しない（呼ばれない）

信頼の根拠が iOS のシステム trust store になる結果は他 4 OS と同じ。`SoraSignalingConfig::ca_cert` の契約（指定 PEM のみを使い、system CA と混ぜない）も iOS で維持する。iOS 側で契約を担保する具体的な API 呼び出しと実装骨格は 0039 に閉じる。

親 PR 時点では iOS も他 4 OS と同じ扱い（`src/ssl_verifier.cpp` + `src/ssl_verifier_stub.cpp` の 2 ファイルビルド）で、現行の `src/ssl_verifier.cpp` の挙動をそのまま保つ（現行 `CMakeLists.txt:130` が iOS でも `src/ssl_verifier.cpp` を含めており、`<rtc_base/ssl_roots.h>` の iOS 対応は現行で既に動いている前提）。iOS が検証委譲型に切り替わるのは 0039 マージ時点。

### 信頼ストアの方針（`ca_cert` 未指定時、他 4 OS）

1. OS のシステム CA ストアから信頼アンカーを読み込む（各 OS 実装は子 issue）
2. `LoadSystemSSLRootCertificates` が 1 件も CA を追加できなかった場合は検証失敗にする。フォールバックは持たない
3. ハードコードされた 2 種類の PEM 定数（`isrg_root` の ISRG Root X1、`lets_encrypt_r3` の Let's Encrypt R3 中間）と WebRTC `ssl_roots.h` は 0040 完了時に完全削除する（親 PR〜0039 PR の期間は `ssl_verifier_stub.cpp` 内に閉じ込めた形で残る）

### `ca_cert` 指定時

現状どおり、指定 PEM のみを信頼する。システム CA もハードコードも混ぜない。他 4 OS では `AddCert` を使うため、`include/sora/ssl_verifier.h` の宣言と `src/ssl_verifier.cpp` の実装をともに残す。iOS では別経路で同等の契約を実現する。

### `insecure == true`

現状どおり検証をスキップする。`insecure` の分岐は呼び出し側（`src/websocket.cpp:184`、`src/rtc_ssl_verifier.cpp:55`）に据え置き、`SSLVerifier::VerifyX509` のシグネチャは変更しない。iOS の `src/ssl_verifier_ios.mm` も同じシグネチャを持ち、呼び出し側の変更は不要。

### `X509_V_FLAG_TRUSTED_FIRST`

他 4 OS 用の `src/ssl_verifier.cpp:193` の `X509_STORE_set_flags(store, X509_V_FLAG_TRUSTED_FIRST)` は維持する。BoringSSL では `X509_VERIFY_PARAM` の初期化時にこのフラグが実質デフォルト有効のため、明示的な set は現行動作を変えない保険としての意味合いが強い（他 OpenSSL 実装への移植性配慮）。システム CA ストアには多数のアンカーとクロス署名が混在するため、明示 set のまま残す。

iOS が検証委譲型に切り替わる 0039 マージ後は BoringSSL の X509 検証を通らないため、iOS 実装ではこのフラグは該当しない（`SecTrustEvaluateWithError` 側が chain building を担う）。

### 他 4 OS 用ヘルパー `LoadSystemSSLRootCertificates`

`SSLVerifier` に `LoadSystemSSLRootCertificates` を追加する。

```cpp
// include/sora/ssl_verifier.h
namespace sora {

class SSLVerifier {
 public:
  static bool VerifyX509(X509* x509,
                         STACK_OF(X509) * chain,
                         const std::optional<std::string>& ca_cert);

 private:
  // PEM 形式のルート証明書を X509_STORE に追加する（ca_cert 指定時に使用）
  static bool AddCert(const std::string& pem, X509_STORE* store);

  // OS のシステム CA を X509_STORE に載せる（他 4 OS 用ヘルパー）
  static bool LoadSystemSSLRootCertificates(X509_STORE* store);
};

}  // namespace sora
```

`LoadBuiltinSSLRootCertificates` は `SSLVerifier` クラスから完全に消滅させる（宣言・実装ともに `SSLVerifier::` のメンバとしては残さない）。旧実装の中身は `src/ssl_verifier_stub.cpp` 内で書き直す（配置の詳細は「暫定実装ソースと OS 別実装ソースの切り替え」節）。

`AddCert` / `LoadSystemSSLRootCertificates` はヘッダ上は宣言だが、iOS ターゲットではこれらの定義を含む TU（`src/ssl_verifier.cpp` / `src/ssl_verifier_stub.cpp`）を CMake でビルド対象から除外するためリンクされない。両関数はいずれも `SSLVerifier` の `private static` メンバであり、SDK 利用者から直接参照される API 経路は存在しないため、iOS ターゲットで未解決シンボルにはならない。将来 iOS 実装から誤って `AddCert` / `LoadSystemSSLRootCertificates` を呼ばないことは、iOS 側の実装制約として維持する必要がある（0039 側で明示する）。

`LoadSystemSSLRootCertificates` の契約（他 4 OS 用）:

- **戻り値**: 1 件以上のアンカーを `X509_STORE_add_cert` で追加できたら `true`、それ以外は `false`
- **部分失敗の扱い**: OS API から取得した個々の cert について `X509_STORE_add_cert` が失敗しても、そのタイミングでは即座に `false` を返さず、個々の失敗ごとに `RTC_LOG(LS_WARNING)` で英語 1 行のログを出して続行する（現行の `LoadBuiltinSSLRootCertificates` の `src/ssl_verifier.cpp:138-142` と同型）。関数終了時に「追加できた件数が 0 かどうか」で最終的な戻り値を決める
- **重複エラーの扱い**: BoringSSL は `X509_STORE_add_cert` で同一 subject の CA に対しバージョンにより挙動が分かれる（`X509_R_CERT_ALREADY_IN_HASH_TABLE` を積んで `return 0` する版と silently ignore で `return 1` する版）。`0` が返った直後に `ERR_peek_last_error()` を検査し、reason が `X509_R_CERT_ALREADY_IN_HASH_TABLE` の場合は `ERR_get_error()` でエラーキューをクリアするだけで済ませ、`RTC_LOG(LS_WARNING)` は出さず成功件数にも数えない（silently ignore する版では `0` を返さないためこの分岐に入らない）。他 reason は WARNING を出して続行する。この扱いを子実装間で共通化する
- **呼び出し側**: `VerifyX509` は `false` を受けたら検証失敗にする
- **呼び出し頻度**: `VerifyX509` から毎回。プロセス寿命キャッシュは持たない。理由は、TLS ハンドシェイクは接続確立時のみで頻度が高くないこと、実装の単純化により OS 別実装間の差異を封じ込めやすいこと、キャッシュ寿命・無効化の判断（証明書ローテーション反映のタイミング）を持ち込まないこと
- **スレッド安全性**: 複数スレッド（Signaling スレッド、ICE スレッド）から並列に呼ばれる前提で、スレッドセーフに実装する。OS API 側のスレッド安全性前提は各子 issue で確認する
- **失敗時ログ**: 「1 件も追加できなかった」で `false` を返すときに `RTC_LOG(LS_ERROR)` で英語 1 行を出す。関数名と失敗の分類（OS API エラー / 0 件追加など）を含める
- **`Guard` パターン**: 各子実装で必要な `Guard` 構造体（現行 `src/ssl_verifier.cpp:176-180` と同型）は TU ローカルに独立コピーを持ち、共通ヘッダには切り出さない（依存関係を増やさない方針）

iOS では `SSLVerifier::VerifyX509` のシグネチャ（`bool(X509*, STACK_OF(X509)*, const std::optional<std::string>&)`）は維持する。iOS 実装内で BoringSSL の `X509` 型と `STACK_OF(X509)` を Security.framework の `SecCertificateRef` に変換して検証委譲する。具体的な変換手順は 0039 に閉じる。呼び出し側 API（`src/websocket.cpp` / `src/rtc_ssl_verifier.cpp`）は全 OS で無変更。

### 暫定実装ソースと OS 別実装ソースの切り替え

親 PR 時点では OS 別実装が揃っていないため、iOS を含む全 OS 一様に「共通の `src/ssl_verifier.cpp` + 暫定 `src/ssl_verifier_stub.cpp`」の 2 ファイルビルドとする。`src/ssl_verifier_stub.cpp` の内容は次節「暫定実装の戻り値と構造」に示す。iOS の検証委譲型への切り替えは 0039 で行う。

同一メンバ関数の定義が同一 link unit（`libsora` の静的ライブラリ）に 2 つ含まれるとリンク時に multiple definition エラーになるため、`ssl_verifier_stub.cpp` と OS 別ソースは CMake の条件付き `target_sources()` で排他選択する。`#if` によるコンパイル時分岐は使わない。

親 PR で `CMakeLists.txt` に追加する分岐骨格（現行の `CMakeLists.txt:99-136` の `target_sources()` ブロックから `src/ssl_verifier.cpp`（現行 130 行）を削除し、`)`（現行 136 行）の直後に以下を挿入する）:

```cmake
if (SORA_TARGET_OS STREQUAL "ubuntu")
  set(SORA_SSL_VERIFIER_SOURCES src/ssl_verifier.cpp src/ssl_verifier_stub.cpp)
elseif (SORA_TARGET_OS STREQUAL "macos")
  set(SORA_SSL_VERIFIER_SOURCES src/ssl_verifier.cpp src/ssl_verifier_stub.cpp)
elseif (SORA_TARGET_OS STREQUAL "windows")
  set(SORA_SSL_VERIFIER_SOURCES src/ssl_verifier.cpp src/ssl_verifier_stub.cpp)
elseif (SORA_TARGET_OS STREQUAL "ios")
  set(SORA_SSL_VERIFIER_SOURCES src/ssl_verifier.cpp src/ssl_verifier_stub.cpp)
elseif (SORA_TARGET_OS STREQUAL "android")
  set(SORA_SSL_VERIFIER_SOURCES src/ssl_verifier.cpp src/ssl_verifier_stub.cpp)
else ()
  message(FATAL_ERROR "Unknown SORA_TARGET_OS: ${SORA_TARGET_OS}")
endif ()
target_sources(sora PRIVATE ${SORA_SSL_VERIFIER_SOURCES})
```

- `SORA_TARGET_OS` は `CMakeLists.txt:20-62` で `windows` / `macos` / `ios` / `android` / `ubuntu` のいずれかにセットされる（Raspberry Pi OS も `ubuntu` に集約される）
- `else` の `FATAL_ERROR` は防御的ガード。現行 `CMakeLists.txt:20-62` は未知の `SORA_TARGET` を素通しにするが、`SORA_SSL_VERIFIER_SOURCES` を確定させるにはここで OS が確定している必要があるため明示的にエラーにする
- 各子 PR は自 OS の分岐ブロックの `set` 行のみを書き換えるため、子 PR 間で同一行を触らず CMakeLists.txt のコンフリクトは発生しない。5 分岐が同一内容で並ぶ冗長さは、この「子 PR 間で 3-way マージが競合しない」ためのコストとして許容する
- CMake 内には issue 番号への言及コメント（`# 子 XXXX で 〜` など）を残さない（`shiguredo-issues` の規約に従い、ソースコード本体に issue 番号を持ち込まない）
- 0040 完了時にはこの分岐骨格を解体して各 OS 別ソース直接指定に置き換え、`ssl_verifier_stub.cpp` も削除する

各子 PR での分岐書き換え（想定）:

- 0036 / 0037 / 0038 (Ubuntu / macOS / Windows): 対応する分岐の `set` 行の 2 番目のファイルを `src/ssl_verifier_stub.cpp` から表に示す OS 別ソース（`src/ssl_verifier_ubuntu.cpp` / `src/ssl_verifier_macos.cpp` / `src/ssl_verifier_windows.cpp`）に置換
- 0039 (iOS): 分岐 4 個目の `set` 行を丸ごと `set(SORA_SSL_VERIFIER_SOURCES src/ssl_verifier_ios.mm)` に置換（iOS のみ `src/ssl_verifier.cpp` も除外し 1 ファイルに）
- 0040 (Android): 分岐 5 個目の `set` 行を同様に `src/ssl_verifier_android.cpp` に置換し、加えて `SORA_SSL_VERIFIER_SOURCES` 変数を含む分岐骨格そのものを解体して各 OS 別ソースの直接指定に置き換え、`ssl_verifier_stub.cpp` を削除する

### 暫定実装の戻り値と構造（stub 専用の例外）

暫定実装は「非破壊」を最優先とし、共通契約とは別の暫定セマンティクスで動く。共通契約との差分は次のとおりで、これらを `src/ssl_verifier_stub.cpp` 冒頭のコメントで列挙する:

1. `AddCert(isrg_root, store)` または `AddCert(lets_encrypt_r3, store)` に失敗したら即座に `false` を返す（現行の `src/ssl_verifier.cpp:199-206` と同じ挙動。共通契約の「部分失敗は続行」は適用しない）
2. WebRTC 組み込みルート追加ヘルパー（下記コード骨格で `SSLVerifier::LoadSystemSSLRootCertificates` 内に直書きする for ループ）と `X509_STORE_set_default_paths` の戻り値は無視し、`AddCert` 2 本が両方成功していれば `true` を返す
3. 上記 for ループ内の `X509_STORE_add_cert` 失敗時は `RTC_LOG(LS_WARNING) << "Unable to add certificate."` を出して続行する（現行 `src/ssl_verifier.cpp:138-142` の WARNING メッセージと出力条件のみ踏襲し、`count_of_added_certs` の集計はしない — 項目 2 参照）。共通契約の「重複エラー（`X509_R_CERT_ALREADY_IN_HASH_TABLE`）の分岐処理」は導入せず、エラーキューも触らない。ISRG Root X1 が `AddCert` と WebRTC 組み込みの両方から来て `X509_STORE_add_cert` が 0 を返し WARNING が乱発することは、暫定期間の許容範囲として扱う
4. 共通契約が求める「0 件時の `RTC_LOG(LS_ERROR)` 1 行」は暫定実装では出さない（`AddCert` 内部の `RTC_LOG(LS_ERROR)` で十分。共通契約の追加 ERROR ログは OS 別実装で発火させる）
5. 現行の診断ログ `RTC_LOG(LS_INFO) << "default cert file: " << X509_get_default_cert_file()`（`src/ssl_verifier.cpp:212`）は暫定実装内でも復元しない（親 PR の完了条件で削除するため）
6. `Guard` パターンは使わない（現行 `LoadBuiltinSSLRootCertificates` が使っていない）。`X509_free(cert)` は個別呼び出し

ファイル構造（想定）:

```cpp
// src/ssl_verifier_stub.cpp
//
// 暫定実装。共通契約との差分は本コメント欄に列挙する（差分項目 1〜6 は
// 親 issue 0035 「暫定実装の戻り値と構造」節参照）。0040 で本ファイルは削除する。

#include "sora/ssl_verifier.h"

#include <cstddef>

#include <rtc_base/logging.h>
#include <rtc_base/ssl_roots.h>

#include <openssl/base.h>
#include <openssl/x509.h>

namespace sora {

namespace {

// 現行 src/ssl_verifier.cpp:23-62 の定義を丸ごと移動 (コメント行含む)
const char isrg_root[] = R"(
...
)";

// 現行 src/ssl_verifier.cpp:64-97 の定義を丸ごと移動 (コメント行含む)
const char lets_encrypt_r3[] = R"(
...
)";

}  // namespace

bool SSLVerifier::LoadSystemSSLRootCertificates(X509_STORE* store) {
  if (!AddCert(isrg_root, store)) {
    return false;
  }
  if (!AddCert(lets_encrypt_r3, store)) {
    return false;
  }
  // 現行 src/ssl_verifier.cpp:127-147 の LoadBuiltinSSLRootCertificates 相当。
  // 抽出せず直書きにする（暫定期間のみで消えるため、premature abstraction を避ける）
  for (size_t i = 0;
       i < sizeof(kSSLCertCertificateList) / sizeof(kSSLCertCertificateList[0]);
       ++i) {
    const unsigned char* cert_buffer = kSSLCertCertificateList[i];
    size_t cert_buffer_len = kSSLCertCertificateSizeList[i];
    X509* cert = d2i_X509(nullptr, &cert_buffer, cert_buffer_len);  // NOLINT
    if (cert == nullptr) {
      continue;
    }
    int r = X509_STORE_add_cert(store, cert);
    if (r == 0) {
      RTC_LOG(LS_WARNING) << "Unable to add certificate.";
    }
    X509_free(cert);
  }
  X509_STORE_set_default_paths(store);  // 戻り値は捨てる（現行踏襲）
  return true;
}

}  // namespace sora
```

- `SSLVerifier::LoadSystemSSLRootCertificates` はクラス外定義であり、C++ の言語規則上 `namespace sora` の直下（クラスを含む名前空間の直下）にしか書けない。匿名名前空間内には置けない
- `AddCert` は `private static` メンバだが、`LoadSystemSSLRootCertificates` 自身が `SSLVerifier` のメンバ関数であるため、クラススコープでアクセスが通る
- 匿名名前空間内の PEM 定数は TU ローカルで、他 TU との衝突を起こさない
- スレッド安全性・呼び出し頻度は共通契約と同じ扱い（毎回列挙、キャッシュなし。1 スレッド 1 ストアの前提）

## 完了条件

### 親 PR（0035）でマージ時点に満たす条件

- `SSLVerifier::LoadSystemSSLRootCertificates(X509_STORE*)` が `include/sora/ssl_verifier.h` に `private static` として宣言されている（`include/sora/ssl_verifier.h:24` の旧 `LoadBuiltinSSLRootCertificates` 宣言を差し替える）
- `SSLVerifier::LoadBuiltinSSLRootCertificates` が `SSLVerifier` クラスから完全に消滅している（宣言・実装ともに `SSLVerifier::` のメンバとしては存在しない）
- `SSLVerifier::VerifyX509` の `ca_cert` 未指定分岐が `LoadSystemSSLRootCertificates(store)` の呼び出し 1 本に置き換わり、戻り値 `false` なら検証失敗にする
- `src/ssl_verifier.cpp` から `isrg_root` / `lets_encrypt_r3` の PEM 定数（`src/ssl_verifier.cpp:23-97`）が削除されている
- `src/ssl_verifier.cpp` から旧 `LoadBuiltinSSLRootCertificates` 実装（`src/ssl_verifier.cpp:127-147`）が削除されている
- `src/ssl_verifier.cpp` から `<rtc_base/ssl_roots.h>` の include（`src/ssl_verifier.cpp:11`）が削除されている
- `SSLVerifier::VerifyX509` の `ca_cert` 未指定分岐（`src/ssl_verifier.cpp:199-211`）から旧ロード処理 4 つ（`AddCert(isrg_root)` / `AddCert(lets_encrypt_r3)` / `LoadBuiltinSSLRootCertificates` / `X509_STORE_set_default_paths`）の直接呼び出しが削除されている
- 同分岐内の診断ログ `RTC_LOG(LS_INFO) << "default cert file: " << X509_get_default_cert_file()`（`src/ssl_verifier.cpp:212`）が削除されている
- `X509_STORE_set_default_paths` の呼び出しは `SSLVerifier::VerifyX509` 本体からは消えるが、暫定期間中は `src/ssl_verifier_stub.cpp` 内の `LoadSystemSSLRootCertificates` 暫定実装が呼ぶためプロセス全体としての呼び出しは残る
- `src/ssl_verifier_stub.cpp` が新規追加され、暫定実装（`SSLVerifier::LoadSystemSSLRootCertificates` の定義、PEM 定数、`kSSLCertCertificateList` の for ループ直書き、`<rtc_base/ssl_roots.h>` の include）を「暫定実装の戻り値と構造」節に示す構造で保持している
- `CMakeLists.txt` の既存 `target_sources()`（`CMakeLists.txt:99-136`）から `src/ssl_verifier.cpp`（`CMakeLists.txt:130`）が除外され、代わりに前述の分岐骨格（`SORA_SSL_VERIFIER_SOURCES` 変数と `target_sources(sora PRIVATE ${SORA_SSL_VERIFIER_SOURCES})`）が追加されている。どの `SORA_TARGET_OS` を選択しても `SORA_SSL_VERIFIER_SOURCES` は `src/ssl_verifier.cpp` + `src/ssl_verifier_stub.cpp` の 2 ファイルにセットされる
- `include/sora/ssl_verifier.h` の `AddCert` の宣言と `src/ssl_verifier.cpp` の実装は `ca_cert` 指定時のために残っている（削除しない）
- `SSLVerifier` は非 static データメンバを持たないためクラスサイズは変化せず、`private static` メンバの削除は libsora の内部シンボル削除に留まる。`include/sora/ssl_verifier.h` を include している下流は通常の再コンパイル・再リンクで取り込める（公開 API シグネチャの破壊は伴わない）
- `ca_cert` 指定時と `insecure == true` の既存挙動が回帰していない
- テスト戦略節で指定するローカル・CI・E2E 確認をすべて実施し、PR 本文に結果を記載している
- CHANGES.md に親 PR 分の `[UPDATE]` エントリ 1 本を追加している（詳細は「CHANGES.md の記載方針」節）

### 全体（0040 完了時）に満たす条件

- 全子 issue 0036〜0040 が closed になっている
- `ca_cert` 未指定時の信頼ストアが各 OS のシステム CA のみになっている（他 4 OS: BoringSSL の `X509_STORE` にシステム CA を投入。iOS: `SecTrustEvaluateWithError` に検証委譲）
- `src/ssl_verifier_stub.cpp` が削除されている
- `CMakeLists.txt` の切り替え分岐（`SORA_SSL_VERIFIER_SOURCES` 変数と `target_sources(sora PRIVATE ${SORA_SSL_VERIFIER_SOURCES})`）が解体され、`if (SORA_TARGET_OS STREQUAL "ubuntu") ... elseif ... else () message(FATAL_ERROR ...) endif ()` 内で各 OS 別ソースが `target_sources(sora PRIVATE ...)` 直接指定に置き換わっている（他 4 OS は `src/ssl_verifier.cpp` + `src/ssl_verifier_<os>.cpp` の 2 ファイル、iOS は `src/ssl_verifier_ios.mm` 単独）
- プロセス全体としても `X509_STORE_set_default_paths` の呼び出しが完全に消滅している（`SSLVerifier::VerifyX509`・`ssl_verifier_stub.cpp`・OS 別実装のいずれからも呼ばれない）
- CHANGES.md に親 PR 分と子 0036〜0040 分のエントリが揃っている（分類は次節）

`src/sora_signaling.cpp:596-599` のコメント（「rtc_base/ssl_roots.h には Let's Encrypt が無い」旨）の更新は 0040 の完了条件に閉じる。

## 解決方法

### 具体的な変更手順（親 PR）

手順 1〜4 は 1 コミット（または 1 PR 内の連続コミット）で一括反映する。個別手順の途中で `python3 run.py build` を試すとリンクエラーや未解決宣言でビルドは通らない（例: 手順 1 のみで宣言不整合、手順 2 のみで未実装リンクエラー、手順 3 のみで CMake 未登録によるリンクエラー）。手順 5・6 は独立して先後可能。

1. `include/sora/ssl_verifier.h`: `include/sora/ssl_verifier.h:24` の `LoadBuiltinSSLRootCertificates` の宣言を削除し、`LoadSystemSSLRootCertificates` の宣言を追加する。`AddCert` の宣言は残す
2. `src/ssl_verifier.cpp`: `isrg_root` / `lets_encrypt_r3` の PEM 定数（`src/ssl_verifier.cpp:23-97`）、`LoadBuiltinSSLRootCertificates` の実装（`src/ssl_verifier.cpp:127-147`）、`<rtc_base/ssl_roots.h>` の include（`src/ssl_verifier.cpp:11`）を削除する。`VerifyX509` の `ca_cert` 未指定分岐（`src/ssl_verifier.cpp:199-212`）を `LoadSystemSSLRootCertificates(store)` の呼び出し 1 本に置き換え、戻り値 `false` なら検証失敗にする。この置き換えに伴い `X509_STORE_set_default_paths` の直接呼び出しと `RTC_LOG(LS_INFO) << "default cert file: ..."` の診断ログ（`src/ssl_verifier.cpp:212`）も同時に消える。`AddCert` の実装と `X509_STORE_set_flags(store, X509_V_FLAG_TRUSTED_FIRST)`（`src/ssl_verifier.cpp:193`）は残す
3. `src/ssl_verifier_stub.cpp` を新規作成する。前述の「暫定実装の戻り値と構造」節に示した構造で PEM 定数（匿名名前空間内）、`<rtc_base/logging.h>` と `<rtc_base/ssl_roots.h>` の include、`namespace sora` 直下の `SSLVerifier::LoadSystemSSLRootCertificates` 暫定実装（内部で `AddCert` 2 本と WebRTC 組み込みルート追加の for ループを直書き）を配置する
4. `CMakeLists.txt`: 現行 `target_sources()`（`CMakeLists.txt:99-136`）から `src/ssl_verifier.cpp`（`CMakeLists.txt:130`）を削除し、`)`（`CMakeLists.txt:136`）の直後に「暫定実装ソースと OS 別実装ソースの切り替え」節の分岐骨格を追加する。全ターゲットで `src/ssl_verifier.cpp` + `src/ssl_verifier_stub.cpp` の 2 ファイルビルドになる
5. `src/sora_signaling.cpp:596-599` のコメントは親 PR では更新しない（暫定実装は現行ロード処理のままなので、更新すると実装と説明が乖離する）。0040 PR で更新する
6. `CHANGES.md` に親 PR 分の `[UPDATE]` エントリを追加する（詳細は次節）

### CHANGES.md の記載方針

`shiguredo-changelog` に従い、各 PR で個別に `CHANGES.md` エントリを追加する。全 PR 分を集約したエントリは書かない。

各子 PR (0036〜0040) の merge 時点で、当該 OS の信頼ストアがシステム CA のみに切り替わる。これは当該 OS の SDK 利用者に対して下位互換のない変更であり、`[CHANGE]` に分類する（`[UPDATE]` ではない）。

- 親 PR: `[UPDATE]` エントリを 1 本追加する。タイトルは「TLS 検証の他 4 OS 用ヘルパー `SSLVerifier::LoadSystemSSLRootCertificates` を追加する」。エントリ本文は親 PR の完了条件節を要約する。この時点では実挙動は現行維持のため `[CHANGE]` にはしない
- 各子 PR (0036〜0040): `[CHANGE]` で当該 OS の TLS 検証がシステム CA のみに切り替わる旨と、移行手段（「システム CA を用意できない環境では `SoraSignalingConfig::ca_cert` に PEM を明示指定する」）を記載する。詳細な文面と OS 固有の補足（iOS の Security.framework 委譲、0040 の `ssl_verifier_stub.cpp` / ハードコード PEM 削除の追加エントリなど）は各子 issue の完了条件に閉じる

### 段階分けとブランチ運用

親 PR が develop にマージされたのち、各子ブランチは develop から分岐する。子 0036〜0039 のマージ順序は任意（前述のとおり CMakeLists.txt のコンフリクトは発生しない）。

**0040 は 0036〜0039 が全て develop にマージされたのちにマージする（必須制約）**。0040 は `ssl_verifier_stub.cpp` の削除と CMake の切り替え分岐の解体を含むため、0036〜0039 のいずれかが未マージの状態で 0040 が先に develop に入ると、未マージの子 PR がベースを更新したときに stub 参照を失ってビルド不能になる。

各子 PR のブランチ名は `shiguredo-git` の命名規則（後方互換のない変更は `feature/change-`）に合わせ、`feature/change-system-ca-store-<os>` に統一している（子 issue 0036〜0040 の `Branch:` フィールドは本 issue 一連の polish 内で `feature/add-` から書き換え済み）。子 issue のファイル名の動詞部分は `-add-` を維持する（起票時のスラッグを保つ運用）。親 issue の `Branch:` は親 PR 単独ではなくプロジェクト全体（親 + 子 5）の総称ブランチ名として `feature/change-tls-trust-store-system-ca` を採る。親 PR そのものの分類は `[UPDATE]` だが、issue が表す作業全体は 0040 完了時点で後方互換のない変更に到達するため `change-` を選ぶ。

## 変更対象ファイル

親 PR で変更するファイル（詳細は「具体的な変更手順（親 PR）」に一本化）:

- `include/sora/ssl_verifier.h`
- `src/ssl_verifier.cpp`
- `src/ssl_verifier_stub.cpp`（新規）
- `CMakeLists.txt`
- `CHANGES.md`

呼び出し側 API（`src/websocket.cpp` / `src/rtc_ssl_verifier.cpp`）は親 PR で無変更（`SSLVerifier::VerifyX509` のシグネチャを維持するため）。`src/sora_signaling.cpp:596-599` のコメントは 0040 で更新する。

## テスト戦略

親 PR で担保する内容:

- 対象ビルドターゲット節の表に列挙した全 9 ターゲットで `python3 run.py build <target>` が通ること。実装者ローカルではホスト OS で扱える 1〜2 ターゲット（例: Mac ホストなら `macos_arm64`）を担保し、残りは GitHub Actions の CI ジョブに担保させる。該当 CI 実行 URL を PR 本文に添える
- sumomo で Sora Labo 相当の公開 CA サーバーに WSS で接続でき、TURN-TLS でも通ること（実 CA / 実サーバーを使う。AGENTS.md「モックやスタブは絶対に利用しないこと」に従う）
- `SoraSignalingConfig::ca_cert` 明示指定時と `insecure == true` の既存挙動が回帰していないこと

E2E テストは `e2e-test/` 配下の `test_sumomo_basic.py` に既存の TLS 経路（`ca_cert` 未指定パス、システム CA 経由の実接続）を通るテストケースを最低 1 件選ぶ。具体的なテストケース名は PR 作成時点の `test_sumomo_basic.py` の実装に合わせて確定させ PR 本文に記載する。CLAUDE.md 記載の形式（`uv run --directory=e2e-test pytest test_sumomo_basic.py::<name> -v -s --timeout=60`）で実行する。Sora Labo 相当の資格情報は `e2e-test/conftest.py` の慣行に従い、環境変数 `TEST_SIGNALING_URL` / `TEST_CHANNEL_ID_PREFIX` / `TEST_SECRET_KEY`（HS256 で access_token を署名する秘密鍵）を `.env` または実行環境の環境変数で渡す。access_token はテスト実行時に conftest.py が JWT で生成する。

`ca_cert` 明示指定パスの E2E テストは既存 `e2e-test/` 配下に存在しないため親 PR のスコープに含めない。`ca_cert` 明示指定パスは実装レベルで `SSLVerifier::AddCert`（`src/ssl_verifier.cpp:99-124`）が親 PR で無変更のままであることを PR レビューで担保する。

親 PR の CI 実効性: 新契約の失敗パス（`LoadSystemSSLRootCertificates` が `false` を返して検証が失敗する経路）は暫定 stub では発火しないため、親 PR では実行時検証されない。この経路の実効テストは各子 PR で担う。

各 OS 固有の接続確認は各子 issue の完了条件に閉じる。

## 親 issue のライフサイクル

親 PR がマージされても、親 issue はすぐに close しない。`issues/` 直下に open のまま残し、`Completed:` は空のままにする。0040 が完了して破壊的変更が確定した日に `Completed:` を埋めて `issues/closed/` に移動する。親 issue の `Completed:` は 0040 close 日と一致させる。
