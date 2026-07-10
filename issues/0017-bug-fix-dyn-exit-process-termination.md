# DYN_REGISTER マクロが exit(1) でプロセスを強制終了する

- Priority: High
- Created: 2026-07-10
- Completed: {YYYY-MM-DD}
- Model: DeepSeek V4 Pro
- Branch: feature/fix-dyn-exit-process-termination
- Polished: {YYYY-MM-DD}

## 目的

`include/sora/dyn/dyn.h` の `DYN_REGISTER` マクロが動的ライブラリの関数解決失敗時に `exit(1)` を呼び、プロセス全体を強制終了する。SDK ライブラリがプロセスを終了させることは許容されない。

## 優先度根拠

CUDA/NvCodec の動的ロードに失敗した場合にアプリケーション全体が問答無用でクラッシュする。ライブラリとして致命的な設計問題であり High。

## 現状

`include/sora/dyn/dyn.h:113-116`:

```cpp
if (f == nullptr) {
    std::cerr << "Failed to GetFunc: " << DYN_STRINGIZE(func)
              << " soname=" << soname << std::endl;
    exit(1);
}
```

また `dyn.h:4` で `#include <iostream> // IWYU pragma: export` により `<iostream>` が全利用者に強制伝播している。これは `std::cerr` 出力のためだけに使われており、静的初期化とコンパイル時間の両面で高コスト。

## 設計方針

1. `exit(1)` を `throw std::runtime_error` に置き換える
2. `std::cerr` 出力を `RTC_LOG(LS_ERROR)` に置き換える
3. `<iostream>` のインクルードと IWYU pragma: export を削除する

## 完了条件

- `DYN_REGISTER` マクロで関数解決失敗時に `exit(1)` が呼ばれないこと
- `<iostream>` の IWYU pragma: export が削除されていること
