# ssl_verifier 周辺のコード規約を修正する

- Priority: Low
- Created: 2026-07-20
- Completed: {YYYY-MM-DD}
- Model: qwen3.8-max-preview
- Branch: feature/fix-ssl-verifier-conventions
- Polished: {YYYY-MM-DD}

## 目的

`feature/change-tls-trust-store` ブランチで追加された ssl_verifier 関連コードに存在する AGENTS.md 規約違反（全角半角スペース、命名不統一、不正確なコメント、不要な include）を修正する。

## 優先度根拠

機能に影響しない規約違反であり、マージ後のフォローアップで対応可能なため Low。

## 現状

### 全角と半角の間に半角スペースがない（AGENTS.md 違反）

| ファイル:行 | 該当箇所 |
|---|---|
| `src/ssl_verifier/ssl_verifier_util.h:56` | `（WARNING ログなし）` |
| `src/ssl_verifier/ssl_verifier_android.cpp:82` | `（Android バージョン` |
| `src/ssl_verifier/ssl_verifier_android.cpp:131` | `（Android 14 以降で提供）` |
| `src/ssl_verifier/ssl_verifier_android.cpp:133` | `（Android 10-13 の主要ストア、Android 14+ でも残る）` |
| `src/ssl_verifier/ssl_verifier_macos.cpp:45` | `CFIndex（long）を返すが` |
| `src/ssl_verifier/ssl_verifier_macos.cpp:49` | `（TryAddCertToStore と同型）` |
| `src/ssl_verifier/ssl_verifier_windows.cpp:52` | `ctx（および` |
| `src/ssl_verifier/ssl_verifier_windows.cpp:55` | `（TryAddCertToStore と同型）` |
| `CHANGES.md:21` | `全 OS（macOS, Linux, Windows, iOS, Android）で` |
| `CHANGES.md:29` | `旧ハードコード PEM（isrg_root / lets_encrypt_r3）と` |

### 命名不統一: `Pem` vs `PEM`

- `src/ssl_verifier/ssl_verifier_ios.mm:45`: `LoadCACertsFromPem`
- `src/ssl_verifier.cpp:27`: `LoadCertsFromPEM`
- `src/ssl_verifier/ssl_verifier_util.h:79`: `ParsePEMCerts`

`PEM` に統一する。

### iOS の hostname 検証コメントが不正確

`src/ssl_verifier/ssl_verifier_ios.mm:99`:

```cpp
// hostname 検証は BoringSSL 側で行うため、ここでは basic X509 policy のみ
```

iOS では BoringSSL の証明書検証パスは通らない。hostname 検証は WebRTC の SSL 接続層で行われる。「WebRTC の SSL 接続層で行うため」に修正する。

### 不要な include

- `src/ssl_verifier.cpp:3`（`<cstddef>`）、`:9`（`<openssl/err.h>`）、`:10`（`<openssl/stack.h>`）
- `src/ssl_verifier/ssl_verifier_ios.mm:9`（`<openssl/bio.h>`）、`:11`（`<openssl/pem.h>`）

### 重複コメント

`e2e-test/sumomo.py:795-797`: `if` の外側と内側で完全に同一のコメント。内側を削除する。

## 完了条件

- 上記の全角半角スペース違反がすべて修正されている
- 命名が `PEM` に統一されている
- iOS の hostname コメントが実態に即している
- 不要な include が削除されている
- 重複コメントが削除されている
- ビルドが成功する
