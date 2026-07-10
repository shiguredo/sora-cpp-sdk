# SendOnDisconnect に多重呼び出しガードがなく OnDisconnect が二重通知される競合

- Priority: High
- Created: 2026-07-10
- Completed: {YYYY-MM-DD}
- Model: DeepSeek V4 Pro
- Branch: feature/fix-send-on-disconnect-double-notify
- Polished: {YYYY-MM-DD}

## 目的

`SendOnDisconnect()` は `state_` をチェックせず無条件に `Clear()` + `OnDisconnect` を post する。以下の競合で `OnDisconnect` が 2 回呼ばれうる:

1. `Connecting` 中の `connection_timeout_timer_` 発火と `Disconnect()` 呼び出し
2. `connection_timeout_timer_` 発火と `OnConnect` の全接続失敗パス

## 優先度根拠

切断通知が二重に届くとアプリケーションの状態管理が破綻する。タイミング依存の競合であり再現が困難なバグ。High。

## 現状

`src/sora_signaling.cpp:1415-1428`:

```cpp
void SoraSignaling::SendOnDisconnect(SoraSignalingErrorCode ec,
                                     std::string message) {
  // state_ のチェックがない
  boost::asio::post([self = shared_from_this(), ec, message]() {
    self->Clear();
    if (auto observer = self->config_.observer.lock()) {
      observer->OnDisconnect(ec, std::move(message));
    }
  });
}
```

## 設計方針

`SendOnDisconnect` に `state_` がすでに `Closed` なら return するガードを追加する。もしくは `disconnect_sent_` フラグで二重送信を防止する。

## 完了条件

- 切断時に `OnDisconnect` が最大 1 回しか呼ばれないこと
