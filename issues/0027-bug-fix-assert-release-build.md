# assert() がリリースビルドで無効化され状態検証が行われない

- Priority: High
- Created: 2026-07-10
- Completed: {YYYY-MM-DD}
- Model: DeepSeek V4 Pro
- Branch: feature/fix-assert-release-build
- Polished: {YYYY-MM-DD}

## 目的

`sora_signaling.cpp` と `rtc_ssl_verifier.cpp` で状態検証に `assert()` を使用しているが、CMake の Release ビルドでは `-DNDEBUG` が定義され `assert()` が no-op となる。特に `rtc_ssl_verifier.cpp:65` の `assert(chain.GetSize() > 0)` はリリースビルドで無効化され、空チェーンに対する `chain.Get(0)` が out-of-bounds アクセスになる危険がある。

## 優先度根拠

リリースビルドで不正状態の検出が行われず、後続コードで未定義動作が発生する。High。

## 現状

```cpp
// sora_signaling.cpp:221
assert(state_ == State::Connected);

// sora_signaling.cpp:653
assert(state_ == State::Connected);

// sora_signaling.cpp:874
assert(state_ == State::Connected || State::Closing || State::Closed);

// rtc_ssl_verifier.cpp:65
assert(chain.GetSize() > 0);
```

## 設計方針

`RTC_CHECK` または `RTC_DCHECK`（リリースビルドでも有効な方）に置き換える。または早期リターン + エラーログに置き換える。

## 完了条件

- 全 `assert()` がリリースビルドでも有効な検証に置き換えられていること
