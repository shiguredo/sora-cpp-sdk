# RTCStatsCallback::OnStatsDelivered で move 後のコールバック再呼び出しリスク

- Priority: Medium
- Created: 2026-07-10
- Polished: 2026-07-10
- Completed: 2026-07-10

## 目的

`src/rtc_stats.cpp` の `OnStatsDelivered` で `std::move(result_callback_)(report)` によりコールバックを消費した後、`result_callback_` は moved-from 状態になる。WebRTC の stats collector が `OnStatsDelivered` を複数回呼んだ場合、空の `std::function` の呼び出しは未定義動作となる。

## 優先度根拠

WebRTC 内部実装に依存するが、複数回呼び出しの可能性がある。未定義動作はクラッシュに直結するため Medium。

## 現状

`src/rtc_stats.cpp:17-20`:

```cpp
void OnStatsDelivered(
    const webrtc::scoped_refptr<const webrtc::RTCStatsReport>& report) override {
  std::move(result_callback_)(report);
  // result_callback_ は moved-from 状態
}
```

## 設計方針

初回呼び出し後に `result_callback_ = nullptr` を設定するか、フラグで二重呼び出しを防止する。

## 完了条件

- `OnStatsDelivered` が複数回呼ばれても未定義動作が発生しないこと
- `src/rtc_stats.cpp:25` の `std::move(result_callback_)(report)` 呼び出し後にガードが追加されていること
- `CHANGES.md` の `## develop` 配下、`### misc` セクションに `[FIX]` エントリを追記する:
  ```
  - [FIX] RTCStatsCallback::OnStatsDelivered で move 後のコールバック再呼び出しリスクを修正する
    - @<担当者>
  ```

## 解決方法

本 issue が前提とするバグは実在しないため、コード修正を行わず closed にする。

- `std::function::operator()` は const 修飾されているため、`std::move(result_callback_)(report)` としても `result_callback_` は moved-from にならず、呼び出し後も有効なまま残る。`std::move` は実質 no-op である（`-std=c++17` で実際にコンパイル実行し、呼び出し後も有効なままであることを確認した）
- 仮に空の `std::function` を呼び出しても、C++17 では未定義動作ではなく `std::bad_function_call` を throw する well-defined behavior である
- `webrtc::RTCStatsCollectorCallback::OnStatsDelivered` は `GetStats()` ごとに 1 回だけ呼ばれる契約であり、全呼び出し箇所 (`src/sora_signaling.cpp`, `examples/sumomo/src/sumomo.cpp`) は毎回 `RTCStatsCallback::Create()` で新規インスタンスを渡している。同一コールバックが再利用されることはない

したがって `src/rtc_stats.cpp` にガードを追加する必要はなく、`CHANGES.md` への `[FIX]` 追記も行わない。
