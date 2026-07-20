# ssl_verifier 周辺のコード規約を修正する

- Priority: Low
- Created: 2026-07-20
- Completed: {YYYY-MM-DD}
- Model: qwen3.8-max-preview
- Branch: feature/refactor-ssl-verifier-conventions
- Polished: 2026-07-20

## 目的

`feature/change-tls-trust-store` ブランチで追加された ssl_verifier 関連コードに存在する AGENTS.md 規約違反（全角半角スペース、命名不統一、不正確なコメント、冗長な include、重複コメント）を修正する。

## 優先度根拠

機能に影響しない規約違反であり、マージ後のフォローアップで対応可能なため Low。

## 現状

前提: `feature/change-tls-trust-store` が develop に未マージの段階で修正する。マージ済みの場合は本 issue の CHANGES.md 修正箇所は別途対応する。

### 全角と半角の間に半角スペースがない（AGENTS.md 違反）

修正方針: AGENTS.md の「全角と半角の間には半角スペースを入れること」に従い、全角括弧と半角英数字の間に半角スペースを入れる。全角文字同士の間（例: `し` と `）`）は両方全角のためスペース不要。

| ファイル:行 | 該当箇所 | 修正後 |
|---|---|---|
| `src/ssl_verifier/ssl_verifier_util.h:56` | `（WARNING ログなし）` | `（ WARNING ログなし）` |
| `src/ssl_verifier/ssl_verifier_android.cpp:82` | `（Android バージョン` | `（ Android バージョン` |
| `src/ssl_verifier/ssl_verifier_android.cpp:131` | `（Android 14 以降で提供）` | `（ Android 14 以降で提供）` |
| `src/ssl_verifier/ssl_verifier_android.cpp:133` | `（Android 10-13 の主要ストア、Android 14+ でも残る）` | `（ Android 10-13 の主要ストア、 Android 14+ でも残る）` |
| `src/ssl_verifier/ssl_verifier_macos.cpp:45` | `CFIndex（long）を返すが` | `CFIndex （ long ）を返すが` |
| `src/ssl_verifier/ssl_verifier_macos.cpp:49` | `（TryAddCertToStore と同型）` | `（ TryAddCertToStore と同型）` |
| `src/ssl_verifier/ssl_verifier_windows.cpp:52` | `ctx（および` | `ctx （および` |
| `src/ssl_verifier/ssl_verifier_windows.cpp:55` | `（TryAddCertToStore と同型）` | `（ TryAddCertToStore と同型）` |
| `CHANGES.md:21` | `全 OS（macOS, Linux, Windows, iOS, Android）で` | `全 OS （ macOS, Linux, Windows, iOS, Android ）で` |
| `CHANGES.md:29` | `旧ハードコード PEM（isrg_root / lets_encrypt_r3）と` | `旧ハードコード PEM （ isrg_root / lets_encrypt_r3 ）と` |

### 命名不統一: `Pem` vs `PEM`

- `src/ssl_verifier/ssl_verifier_ios.mm:45`: `LoadCACertsFromPem` （定義）
- `src/ssl_verifier/ssl_verifier_ios.mm:133`: `LoadCACertsFromPem(*ca_cert)` （呼び出し側）
- `src/ssl_verifier.cpp:27`: `LoadCertsFromPEM`
- `src/ssl_verifier/ssl_verifier_util.h:79`: `ParsePEMCerts`

`PEM` に統一する。`LoadCACertsFromPem` → `LoadCACertsFromPEM`（定義・呼び出し側の両方）。

### iOS の hostname 検証コメントが不正確

`src/ssl_verifier/ssl_verifier_ios.mm:113`:

```cpp
// hostname 検証は BoringSSL 側で行うため、ここでは basic X509 policy のみ
```

iOS では BoringSSL の証明書検証パスは通らない。hostname 検証は WebRTC の SSL 接続層（`RTCSSLVerifier::VerifyChain` の呼び出し元）で行われる。「hostname 検証は WebRTC の SSL 接続層で行うため、ここでは basic X509 policy のみ」に修正する。

### 冗長な include

推移的 include で利用可能なため直接 include が冗長な箇所:

- `src/ssl_verifier.cpp:3`（`<cstddef>`）: `size_t` の直接使用なし。`ssl_verifier_util.h:4` 経由で含む
- `src/ssl_verifier.cpp:9`（`<openssl/err.h>`）: `ERR_*` の直接呼び出しなし。`ssl_verifier_util.h:12` 経由で含む
- `src/ssl_verifier.cpp:10`（`<openssl/stack.h>`）: `ssl_verifier.h:9` 経由で含む
- `src/ssl_verifier/ssl_verifier_ios.mm:9`（`<openssl/bio.h>`）: `BIO_*` の直接呼び出しなし。`ssl_verifier_util.h:11` 経由で含む
- `src/ssl_verifier/ssl_verifier_ios.mm:11`（`<openssl/pem.h>`）: `PEM_*` の直接呼び出しなし。`ssl_verifier_util.h:13` 経由で含む

### 重複コメント

`e2e-test/sumomo.py:795-797`: `if` の外側と内側で完全に同一のコメント。内側 （797 行目） を削除する。

## 設計方針

各修正は独立しており、1 コミットで一括修正する（全修正が同一ブランチ `feature/change-tls-trust-store` 由来の規約違反であり、1 つの論理的な変更であるため）。リネーム （`LoadCACertsFromPem` → `LoadCACertsFromPEM`） は `grep -r "FromPem" src/` で旧名が残っていないことを確認する。

## 完了条件

- 上記の全角半角スペース違反がすべて修正されている
- 命名が `PEM` に統一されている（`grep -r "FromPem" src/` で 0 件）
- iOS の hostname コメントが実態に即している
- 冗長な include が削除されている
- 重複コメントが削除されている
- `python3 run.py build macos_arm64` のビルドが成功する
- `python3 run.py build ios` のビルドが成功する（`.mm` ファイルの include 削除・リネームは macOS ビルドでは検証できないため）
