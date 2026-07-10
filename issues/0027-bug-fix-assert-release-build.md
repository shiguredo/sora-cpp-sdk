# assert() がリリースビルドで無効化され状態検証が行われない

- Priority: High
- Created: 2026-07-10
- Completed: {YYYY-MM-DD}
- Model: DeepSeek V4 Pro
- Branch: feature/fix-assert-release-build
- Polished: 2026-07-10
## 目的

`sora_signaling.cpp` と `rtc_ssl_verifier.cpp` で状態検証に `assert()` を使用しているが、CMake の Release ビルドでは `CMAKE_BUILD_TYPE=Release` により `-DNDEBUG` が付与され `assert()` が no-op となる。特に `rtc_ssl_verifier.cpp:65` の `assert(chain.GetSize() > 0)` はリリースビルドで無効化され、空チェーンに対する `chain.Get(0)` が out-of-bounds アクセス（未定義動作）になる危険がある。

本 issue では、この危険を除去するため、リリースビルドで無効化される `assert()` を、箇所ごとに適切な手段（早期リターン + エラーログ、または `RTC_CHECK`、または削除）へ置き換える。

## 優先度根拠

`rtc_ssl_verifier.cpp:65` はリリースビルドで空チェーンの検出が失われ、直後の `chain.Get(0)` が out-of-bounds アクセスとなり未定義動作を引き起こす。TLS 検証パスでのメモリ安全性の問題であるため High。

## 現状

対象となる `assert()` は以下の 4 箇所である。いずれも `assert()` を使っているため、リリースビルドでは検証が行われない。

```cpp
// src/sora_signaling.cpp:221 (SoraSignaling::Redirect)
assert(state_ == State::Connected);

// src/sora_signaling.cpp:653 (SoraSignaling::DoInternalDisconnect)
assert(state_ == State::Connected);

// src/sora_signaling.cpp:874 (SoraSignaling::OnRead 内、if (ec) ブロック)
assert(state_ == State::Connected || state_ == State::Closing ||
       state_ == State::Closed);

// src/rtc_ssl_verifier.cpp:65 (RTCSSLVerifier::VerifyChain)
assert(chain.GetSize() > 0);
```

補足事項:

- `State` は `enum class` ではなく plain enum（`include/sora/sora_signaling.h:351`）である。
- `sora_signaling.cpp:874` の直後は、`State::Closed` / `State::Closing` / `State::Connected` を個別に分岐処理し、いずれにも当てはまらない場合は `sora_signaling.cpp:924-927` で `INTERNAL_ERROR` を送出してフォールバックしている。つまりこの箇所はリリースビルドでも安全に処理されており、`assert()` はデバッグ用の重複した契約確認である。
- `src/sora_video_codec_factory.cpp:137,178,216,257` にも同種の `assert()`（`config.capability_config.openh264_path` / `config.capability_config.amf_context` の非 null 確認）が存在する。ただしこれらはシグナリングの状態検証ではなくコーデック設定バリデーションという別の関心事であり、本 issue のスコープには含めない（対応する場合は別 issue とする）。

## 設計方針

前提: `RTC_DCHECK` は `assert()` と同様に `NDEBUG` 依存でリリースビルドでは無効化される（`rtc_base/checks.h` の `RTC_DCHECK_IS_ON` ガード）。したがって本 issue の目的（リリースビルドでも検証を有効にする）には使えない。置き換え先は「早期リターン + エラーログ」または `RTC_CHECK`（常時有効・違反時 abort）のいずれかとする。

箇所ごとの方針は以下のとおり。

1. `rtc_ssl_verifier.cpp:65` → 早期リターン + エラーログ

   `chain.GetSize() == 0` の場合に `RTC_LOG(LS_ERROR)`（英語メッセージ）を出力し `return false` する。`VerifyChain` は `bool` を返し、空チェーンは対向・WebRTC 側入力に依存する契約であるため、abort させるより検証失敗として接続を拒否する方が安全であり、かつ out-of-bounds アクセスを確実に回避できる。

2. `sora_signaling.cpp:221`（`Redirect`）／ `sora_signaling.cpp:653`（`DoInternalDisconnect`） → `RTC_CHECK`

   いずれも `void` 関数であり、早期リターンすると呼び出し元がエラーを検知できない。特に `DoInternalDisconnect` で早期リターンすると `state_` が `Closing` に遷移せず、`ws_` / `dc_` のクローズと `OnDisconnect` 通知が行われず、リソースリークとゾンビ状態を招く。これらは「本来到達し得ない」内部状態不変条件であり、違反はプログラミングエラーなので `RTC_CHECK` で即座に検出する。既存コードでも `device_video_capturer.cpp:78` で同様の用途に `RTC_CHECK` を使用している。

3. `sora_signaling.cpp:874`（`OnRead`） → `assert()` を削除する

   前述のとおり直後の分岐と `INTERNAL_ERROR` フォールバックによりリリースビルドでも安全に処理されており、この `assert()` は重複している。不変条件を残したい場合は日本語コメントで意図を記す。

include の扱い:

- `sora_signaling.cpp` に `RTC_CHECK` を使うため `#include <rtc_base/checks.h>` を追加し、`assert()` を使わなくなるため `#include <cassert>`（`sora_signaling.cpp:4`）を削除する。
- `rtc_ssl_verifier.cpp` に `RTC_LOG` を使うため `#include <rtc_base/logging.h>` を追加し、`assert()` を使わなくなるため `#include <cassert>`（`rtc_ssl_verifier.cpp:3`）を削除する。

決定が必要な点:

- `Redirect` / `DoInternalDisconnect` を `RTC_CHECK`（違反時 abort）とするか、あるいはログ出力のみで処理を継続するかは、シグナリングライブラリとしてプロセスを落とすことの是非に関わる。上記は「不正状態のまま処理を続けてゾンビ状態・リソースリークを起こすより abort が安全」という判断による。異論があれば実装前に確定する。

## 完了条件

- 対象 4 箇所の `assert()` が、上記「設計方針」のとおりに置き換え・削除されていること
  - `rtc_ssl_verifier.cpp:65` が早期リターン `false` + `RTC_LOG(LS_ERROR)`（英語メッセージ）に置き換えられ、空チェーンで `chain.Get(0)` に到達しないこと
  - `sora_signaling.cpp:221` / `sora_signaling.cpp:653` が `RTC_CHECK` に置き換えられていること
  - `sora_signaling.cpp:874` の `assert()` が削除されていること
- 不要になった `#include <cassert>` が削除され、必要な `#include <rtc_base/checks.h>`（`sora_signaling.cpp`）・`#include <rtc_base/logging.h>`（`rtc_ssl_verifier.cpp`）が追加されていること
- Release ビルド（`python3 run.py build <target>` のデフォルト）が通り、`test_sumomo_basic.py` の sendonly/recvonly で通常フローが壊れていないこと（`RTC_CHECK` が通常フローで発火しないこと）を確認していること
- `CHANGES.md` の `## develop` 配下、`### misc` セクションより前の `src/` コア修正の `[FIX]` 群に、以下の `[FIX]` エントリを追記する（`### misc` は Examples / CI / tooling 用のため使わない）。担当者は実装者が確定する:
  ```
  - [FIX] assert() がリリースビルドで無効化され状態検証が行われないのを修正する
    - @<担当者>
  ```
