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

- WSS: `src/websocket.cpp:180-190` の verify callback。`preverified == true`（Boost.Asio が OpenSSL default paths で成功した）ならそのまま `true` を返し、`preverified == false` のときのみ `SSLVerifier::VerifyX509` に落として自前検証する
- WebRTC 側の TLS 証明書検証全般（TURN-TLS を含む）: `RTCSSLVerifier`（`src/rtc_ssl_verifier.cpp:54`）→ `SSLVerifier::VerifyX509`。`RTCSSLVerifier` は `sora_signaling.cpp:600-602` で `dependencies.tls_cert_verifier` に登録される
- 設定は `SoraSignalingConfig::insecure`（`include/sora/sora_signaling.h:105`）と `SoraSignalingConfig::ca_cert`（同 163 行）

`ca_cert` 未指定かつ `insecure == false` のとき、`SSLVerifier::VerifyX509`（`src/ssl_verifier.cpp:199-212`）は次の順で信頼ストアを構築する。以降これを「現行ロード処理」と呼ぶ。

1. `AddCert(isrg_root, store)`：ハードコードされた ISRG Root X1 の PEM（`src/ssl_verifier.cpp:23-62` に定義）を `X509_STORE` に追加
2. `AddCert(lets_encrypt_r3, store)`：ハードコードされた Let's Encrypt R3 の PEM（同 64-97 行に定義）を `X509_STORE` に追加
3. `LoadBuiltinSSLRootCertificates(store)`（同 127-147 行）：WebRTC `rtc_base/ssl_roots.h` のルート証明書群を `X509_STORE` に追加
4. `X509_STORE_set_default_paths(store)`：BoringSSL の既定検索パス（コンパイル時デフォルトの `/etc/ssl/cert.pem` / `/etc/ssl/certs` および環境変数 `SSL_CERT_FILE` / `SSL_CERT_DIR`）を検索対象に登録

`ca_cert` 指定時は指定 PEM のみを使い、上記 1〜4 は行わない（この挙動は維持する。`ca_cert` の型は `std::optional<std::string>` で、単一の PEM 文字列内に複数証明書を含めてよい）。

問題点:

- Keychain / Windows 証明書ストア / Android システム CA を読まない
- OS ごとに信頼できる CA 集合が埋め込みルートに引きずられる
- Let's Encrypt 中間証明書のハードコードはルート信頼の本来の役割ではない

## 対象ビルドターゲット

親 issue では全ターゲットを対象とし、OS 固有実装は各子 issue に閉じる。CMake の `SORA_TARGET_OS` 値と対応させる。

| 子 issue | `SORA_TARGET_OS` | 対象 `SORA_TARGET` | OS 別ソースファイル名 |
|---|---|---|---|
| 0036 | `ubuntu` | `ubuntu-22.04_x86_64` / `ubuntu-24.04_x86_64` / `ubuntu-22.04_armv8` / `ubuntu-24.04_armv8` / `raspberry-pi-os_armv8` | `src/ssl_verifier_ubuntu.cpp` |
| 0037 | `macos` | `macos_arm64` | `src/ssl_verifier_macos.mm` |
| 0038 | `windows` | `windows_x86_64` | `src/ssl_verifier_windows.cpp` |
| 0039 | `ios` | `ios`（Device / Simulator の両方） | `src/ssl_verifier_ios.mm` |
| 0040 | `android` | `android` | `src/ssl_verifier_android.cpp` |

Raspberry Pi OS は Debian ベースであり `SORA_TARGET_OS` は `ubuntu` に集約される（`CMakeLists.txt:58-61`）。Ubuntu 系ソース `src/ssl_verifier_ubuntu.cpp` をそのまま使う前提とする。

## 想定する動作環境（システム CA の前提）

各 OS のシステム CA ストアに ISRG Root X1 が収録されていることを前提にする。実務上の下限:

- Ubuntu / Raspberry Pi OS: `ca-certificates` パッケージが導入され、`/etc/ssl/certs/ca-certificates.crt` を含む標準構成であること
- macOS: 現行サポート範囲（Ventura 以降）はいずれも収録済み
- Windows: 現行サポート範囲（Windows 10 以降）で AuthRoot が更新されていれば収録済み
- iOS: サポート最小バージョン以降で収録済み
- Android: DEPS の `ANDROID_NATIVE_API_LEVEL=29`（Android 10）以降を対象とする。この API level のシステム CA ストアには ISRG Root X1 が含まれる

これらを満たさない環境では `SoraSignalingConfig::ca_cert` の明示指定で回避することを、各子 PR の `[CHANGE]` エントリの移行手段として記載する（記載方針は後述）。

## 設計方針

### 信頼ストアの方針（`ca_cert` 未指定時）

1. OS のシステム CA ストアから信頼アンカーを読み込む（各 OS 実装は子 issue）
2. `LoadSystemSSLRootCertificates` が 1 件も CA を追加できなかった場合は検証失敗にする。フォールバックは持たない
3. ハードコードされた Let's Encrypt 証明書と WebRTC `ssl_roots.h` は 0040 完了時に完全削除する（親 PR〜0039 PR の期間は `ssl_verifier_stub.cpp` 内に閉じ込めた形で残る）

### `ca_cert` 指定時

現状どおり、指定 PEM のみを信頼する。システム CA もハードコードも混ぜない。`AddCert` はこの分岐で使うため、`include/sora/ssl_verifier.h` の宣言と `src/ssl_verifier.cpp` の実装をともに残す。

### `insecure == true`

現状どおり検証をスキップする。`insecure` の分岐は呼び出し側（`src/websocket.cpp:184`、`src/rtc_ssl_verifier.cpp:55`）に据え置き、`SSLVerifier::VerifyX509` の既存シグネチャは変更しない。

### `X509_V_FLAG_TRUSTED_FIRST`

`src/ssl_verifier.cpp:193` の `X509_STORE_set_flags(store, X509_V_FLAG_TRUSTED_FIRST)` は維持する。システム CA ストアには多数のアンカーとクロス署名が混在するため、検証時に信頼済みアンカーを優先させる目的でこのフラグは有用（DST Root CA X3 の失効に伴う ISRG Root X1 クロスサイン過渡期を吸収した目的ではなく、システム CA ストア全般での chain building の安定化のために維持する）。

### 共通差し込み口

`SSLVerifier` に `LoadSystemSSLRootCertificates` を追加し、ヘッダから `LoadBuiltinSSLRootCertificates` の宣言を削除する。

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

  // OS のシステム CA を X509_STORE に載せる。
  // 定義は暫定実装（src/ssl_verifier_stub.cpp）または OS 別実装（子 issue で追加）に閉じる。
  static bool LoadSystemSSLRootCertificates(X509_STORE* store);
};

}  // namespace sora
```

`LoadBuiltinSSLRootCertificates` は `SSLVerifier` クラスから完全に消滅させる。旧実装の中身は `src/ssl_verifier_stub.cpp` 内の匿名名前空間のフリー関数として書き直す（`SSLVerifier::` プレフィックスは付けない）。`private static` メンバとして残さない。

`LoadSystemSSLRootCertificates` の契約:

- 戻り値: 1 件以上のアンカーを `X509_STORE_add_cert` で追加できたら `true`、それ以外は `false`
- 部分失敗の扱い: OS API から取得した個々の cert について `X509_STORE_add_cert` が失敗しても、そのタイミングでは即座に `false` を返さず、個々の失敗ごとに `RTC_LOG(LS_WARNING)` で英語 1 行のログを出して続行する（現行の `LoadBuiltinSSLRootCertificates` の 138-139 行と同じ挙動）。関数終了時に「追加できた件数が 0 かどうか」で最終的な戻り値を決める
- 呼び出し側: `VerifyX509` は `false` を受けたら検証失敗にする
- 呼び出し頻度: `VerifyX509` から毎回。プロセス寿命キャッシュは持たない（Premature Optimization is the Root of All Evil、CLAUDE.md）。将来キャッシュを入れる余地は残す前提だが、契約としては現状「毎回列挙」で固定する
- スレッド安全性: 複数スレッド（Signaling スレッド、ICE スレッド）から並列に呼ばれる前提で、スレッドセーフに実装する。OS API 側のスレッド安全性前提は各子 issue で確認する
- 失敗時ログ: 「1 件も追加できなかった」で `false` を返すときに `RTC_LOG(LS_ERROR)` で英語 1 行を出す。関数名と失敗の分類（OS API エラー / 0 件追加など）を含める

呼び出し側 API は変更しない。`src/websocket.cpp` と `src/rtc_ssl_verifier.cpp` は無変更。

### 暫定実装ソースと OS 別実装ソースの切り替え

親 PR 時点では OS 別実装が揃っていないため、`SSLVerifier::LoadSystemSSLRootCertificates` の暫定実装を `src/ssl_verifier_stub.cpp` に配置する。ここに PEM 定数（`isrg_root` / `lets_encrypt_r3`）、旧 `LoadBuiltinSSLRootCertificates` 相当のヘルパー、`<rtc_base/ssl_roots.h>` の include をすべて閉じ込める。OS 別実装は各子 issue で追加する。

同一メンバ関数の定義が同一プログラムに 2 つ含まれると ODR 違反になるため、`ssl_verifier_stub.cpp` と OS 別ソースは CMake の条件付き `target_sources()` で排他選択する。`#if` によるコンパイル時分岐は使わない。

親 PR で `CMakeLists.txt` に追加する分岐骨格:

```cmake
# CMakeLists.txt の target_sources ブロック直後に置く。
# 親 PR 時点では全 OS で ssl_verifier_stub.cpp を選ぶ。
# 各子 PR は該当 OS の elseif ブロックの set 行のみを OS 別ソースに書き換える。
if (SORA_TARGET_OS STREQUAL "ubuntu")
  set(SORA_SYSTEM_CA_IMPL src/ssl_verifier_stub.cpp)   # 子 0036 で src/ssl_verifier_ubuntu.cpp に差し替える
elseif (SORA_TARGET_OS STREQUAL "macos")
  set(SORA_SYSTEM_CA_IMPL src/ssl_verifier_stub.cpp)   # 子 0037 で src/ssl_verifier_macos.mm に差し替える
elseif (SORA_TARGET_OS STREQUAL "windows")
  set(SORA_SYSTEM_CA_IMPL src/ssl_verifier_stub.cpp)   # 子 0038 で src/ssl_verifier_windows.cpp に差し替える
elseif (SORA_TARGET_OS STREQUAL "ios")
  set(SORA_SYSTEM_CA_IMPL src/ssl_verifier_stub.cpp)   # 子 0039 で src/ssl_verifier_ios.mm に差し替える
elseif (SORA_TARGET_OS STREQUAL "android")
  set(SORA_SYSTEM_CA_IMPL src/ssl_verifier_stub.cpp)   # 子 0040 で src/ssl_verifier_android.cpp に差し替える
else ()
  message(FATAL_ERROR "Unknown SORA_TARGET_OS: ${SORA_TARGET_OS}")
endif ()
target_sources(sora PRIVATE ${SORA_SYSTEM_CA_IMPL})
```

- `SORA_TARGET_OS` は `CMakeLists.txt:20-62` で `windows` / `macos` / `ios` / `android` / `ubuntu` のいずれかにセットされる（Raspberry Pi OS も `ubuntu` に集約される）
- 各子 PR は自 OS の `elseif` ブロックの `set` 行のみを書き換えるため、子 PR 間で同一行を触らず CMakeLists.txt のコンフリクトは発生しない
- 0040 完了時にはこの分岐ブロックごと削除し、`ssl_verifier_stub.cpp` も削除する

### 暫定実装の戻り値（stub 専用の例外）

暫定実装は「非破壊」を最優先とし、共通契約とは別の暫定セマンティクスで動く（差異は stub のコードコメントで明示する）。

- `AddCert(isrg_root, store)` または `AddCert(lets_encrypt_r3, store)` に失敗したら `false` を返す（現行の `src/ssl_verifier.cpp:199-206` と同じ挙動）
- WebRTC 組み込みルート追加ヘルパーと `X509_STORE_set_default_paths` の戻り値は無視し、上記が両方成功していれば `true` を返す

## 完了条件

### 親 PR（0035）でマージ時点に満たす条件

- `SSLVerifier::LoadSystemSSLRootCertificates(X509_STORE*)` が `include/sora/ssl_verifier.h` に `private static` として宣言されている
- `SSLVerifier::LoadBuiltinSSLRootCertificates` が `SSLVerifier` クラスから完全に消滅している（宣言・実装ともに `SSLVerifier::` のメンバとしては存在しない）
- `SSLVerifier::VerifyX509` の `ca_cert` 未指定分岐が `LoadSystemSSLRootCertificates(store)` の呼び出し 1 本に置き換わり、戻り値 `false` なら検証失敗にする
- `src/ssl_verifier.cpp` から次のものが削除されている: `isrg_root` / `lets_encrypt_r3` の PEM 定数、旧 `LoadBuiltinSSLRootCertificates` 実装、`<rtc_base/ssl_roots.h>` の include、旧ロード処理 4 つの直接呼び出し、および `ca_cert` 未指定分岐内にあった `RTC_LOG(LS_INFO) << "default cert file: " << X509_get_default_cert_file()`（`src/ssl_verifier.cpp:212`）の診断ログ
- `src/ssl_verifier_stub.cpp` が新規追加され、暫定実装（`SSLVerifier::LoadSystemSSLRootCertificates` の定義、PEM 定数、WebRTC 組み込みルート追加ヘルパー、`<rtc_base/ssl_roots.h>` の include）を保持している
- `CMakeLists.txt` に前述の分岐骨格が追加され、全ターゲットで `ssl_verifier_stub.cpp` が `target_sources()` に追加されている
- `AddCert` の宣言・実装は `ca_cert` 指定時のために残っている
- `X509_STORE_set_flags(store, X509_V_FLAG_TRUSTED_FIRST)` の設定は維持されている
- `ca_cert` 指定時と `insecure == true` の既存挙動が回帰していない
- CHANGES.md に親 PR 分のエントリ 1 本を追加している

### 全体（0040 完了時）に満たす条件

- 全子 issue 0036〜0040 が closed になっている
- `ca_cert` 未指定時の信頼ストアが各 OS のシステム CA のみになっている
- `src/ssl_verifier_stub.cpp` および `CMakeLists.txt` の切り替え分岐が削除され、`target_sources(sora PRIVATE ${SORA_SYSTEM_CA_IMPL})` は各 OS 別ソース直接指定に置き換わっている
- `X509_STORE_set_default_paths` の直接呼び出しが `SSLVerifier::VerifyX509` から削除されている（各 OS 実装内で使うかは各子 issue の判断に委ねる）
- `src/sora_signaling.cpp:596-599` のコメント（「rtc_base/ssl_roots.h には Let's Encrypt が無い」旨）がシステム CA 方針に合わせて更新されている
- CHANGES.md に親 PR 分と子 0036〜0040 分のエントリが揃っている（分類は次節）

## 解決方法

### 具体的な変更手順（親 PR）

1. `include/sora/ssl_verifier.h`: `LoadBuiltinSSLRootCertificates` の宣言を削除し、`LoadSystemSSLRootCertificates` の宣言を追加する
2. `src/ssl_verifier.cpp`: `isrg_root` / `lets_encrypt_r3` の PEM 定数、`LoadBuiltinSSLRootCertificates` の実装、`<rtc_base/ssl_roots.h>` の include を削除する。`VerifyX509` の `ca_cert` 未指定分岐を `LoadSystemSSLRootCertificates(store)` の呼び出し 1 本に置き換え、戻り値 `false` なら検証失敗にする。`AddCert` と `X509_STORE_set_flags(store, X509_V_FLAG_TRUSTED_FIRST)` は残す
3. `src/ssl_verifier_stub.cpp` を新規作成する。次の 3 つを配置する:
   - 匿名名前空間内に PEM 定数（`isrg_root` / `lets_encrypt_r3`）と、旧 `LoadBuiltinSSLRootCertificates` 相当のヘルパー関数（`bool AddBuiltinRoots(X509_STORE* store)` 等の名前で、`SSLVerifier::` プレフィックスは付けない）を置く
   - `<rtc_base/ssl_roots.h>` の include をここで行う
   - `namespace sora` 直下（匿名名前空間の外）に `SSLVerifier::LoadSystemSSLRootCertificates` の暫定実装を置く。中身は `SSLVerifier::AddCert(isrg_root, store)` → `SSLVerifier::AddCert(lets_encrypt_r3, store)` → 匿名名前空間の `AddBuiltinRoots(store)` → `X509_STORE_set_default_paths(store)` を順に呼ぶ
4. `CMakeLists.txt`: 前述の分岐骨格を追加し、全ターゲットで `ssl_verifier_stub.cpp` が `target_sources()` に含まれるようにする
5. コメント（`src/sora_signaling.cpp:596-599`）は親 PR では更新しない（暫定実装は現行ロード処理のままなので、更新すると実装と説明が乖離する）。0040 PR で更新する
6. `CHANGES.md` に親 PR 分のエントリを追加する（分類は次節）

### CHANGES.md の記載方針

`shiguredo-changelog` に従い、各 PR で個別に `CHANGES.md` エントリを追加する。全 PR 分を集約したエントリは書かない。

各子 PR (0036〜0040) の merge 時点で、当該 OS の信頼ストアがシステム CA のみに切り替わる。これは当該 OS の SDK 利用者に対して下位互換のない変更であり、`[CHANGE]` に分類する（`[UPDATE]` ではない）。

- 親 PR: `[UPDATE]` エントリで「TLS 検証の共通差し込み口 `SSLVerifier::LoadSystemSSLRootCertificates` を追加する。挙動は現行維持」の旨を記載する（`SSLVerifier::LoadBuiltinSSLRootCertificates` は `private static` メンバなので削除しても公開 API 破壊は伴わない）
- 子 0036〜0040 PR: 各 `[CHANGE]` エントリで「<OS> の TLS 検証を OS のシステム CA に切り替える」の旨と、移行手段（「システム CA を用意できない環境では `SoraSignalingConfig::ca_cert` に PEM を明示指定する」）を記載する
- 子 0040 PR: 上記 Android 分の `[CHANGE]` に加え、`ssl_verifier_stub.cpp` / ハードコード PEM / WebRTC `ssl_roots.h` 依存の完全削除を追加の `[CHANGE]` エントリとして分ける

### 段階分けとブランチ運用

親 PR が develop にマージされたのち、各子ブランチは develop から分岐する。子 0036〜0039 のマージ順序は任意（前述のとおり CMakeLists.txt のコンフリクトは発生しない）。

**0040 は 0036〜0039 が全て develop にマージされたのちにマージする（必須制約）**。0040 は `ssl_verifier_stub.cpp` の削除と CMake の切り替え分岐の解体を含むため、0036〜0039 のいずれかが未マージの状態で 0040 が先に develop に入ると、未マージの子 PR がベースを更新したときに stub 参照を失ってビルド不能になる。

## 変更対象ファイル

親 PR で変更するファイル（詳細は「具体的な変更手順（親 PR）」に一本化）:

- `include/sora/ssl_verifier.h`
- `src/ssl_verifier.cpp`
- `src/ssl_verifier_stub.cpp`（新規）
- `CMakeLists.txt`
- `CHANGES.md`

親 PR では変更しないファイル:

- `src/websocket.cpp`
- `src/rtc_ssl_verifier.cpp`
- `include/sora/rtc_ssl_verifier.h`
- `include/sora/sora_signaling.h`
- `src/sora_signaling.cpp`

## テスト戦略

親 PR で担保する内容:

- 全ビルドターゲットで `python3 run.py build <target>` が通ること
- sumomo で Sora Labo 相当の公開 CA サーバーに WSS で接続でき、TURN-TLS でも通ること（実 CA / 実サーバーを使う）
- `SoraSignalingConfig::ca_cert` 明示指定時と `insecure == true` の既存挙動が回帰していないこと

E2E テストは `e2e-test/` 配下の TLS 経路を通る sumomo テストケースを 1 件以上回す。回すテストケースの具体名は親 PR 作成時に決めて PR 本文に記載する。CLAUDE.md 記載の形式（`uv run --directory=e2e-test pytest ... -v -s --timeout=60`）で実行する。

各 OS 固有の接続確認は各子 issue の完了条件に閉じる。

## 親 issue のライフサイクル

親 PR がマージされても、親 issue はすぐに close しない。`issues/` 直下に open のまま残し、`Completed:` は空のままにする。0040 が完了して破壊的変更が確定した日に `Completed:` を埋めて `issues/closed/` に移動する。これは `shiguredo-issues` に明文化されていない拡張運用であり、親 issue の作業完了 = 全子 issue 完了とみなす。
