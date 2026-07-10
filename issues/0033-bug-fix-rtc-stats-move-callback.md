# RTCStatsCallback::OnStatsDelivered で move 後のコールバック再呼び出しリスク

- Priority: Medium
- Created: 2026-07-10
- Completed: {YYYY-MM-DD}
- Model: DeepSeek V4 Pro
- Branch: feature/fix-rtc-stats-move-callback
- Polished: {YYYY-MM-DD}

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
