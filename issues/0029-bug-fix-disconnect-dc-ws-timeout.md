# DoInternalDisconnect DC+WS パスで WebSocket close にタイムアウト保護がない

- Priority: High
- Created: 2026-07-10
- Polished: 2026-07-10

## 目的

`DoInternalDisconnect` の `using_datachannel_ && ws_connected_` パスでは DataChannel の切断タイムアウトがあることを理由に WebSocket 側の `closing_timeout_timer_` を使用していない。しかし DataChannel の `Close()` コールバックは成功時 (`!ec`) に早期 return し何もしない。サーバが DataChannel close 後に WebSocket を閉じない場合、`on_ws_close_` は永遠に呼ばれず切断が完了しない。

## 優先度根拠

切断シーケンスがハングし、アプリケーションが終了できなくなる。特定のサーバ実装やネットワーク条件下で発生しうる。High。

## 現状

`src/sora_signaling.cpp:669-727`:

```cpp
if (using_datachannel_ && ws_connected_) {
  // DC の切断タイムアウトがあるので closing_timeout_timer_ を使わない
  // ...
  dc_->Close(..., [self = shared_from_this(), ...](boost::system::error_code ec) {
    if (!ec) {
      return;  // 成功時は早期 return、on_ws_close_ のみが頼り
    }
    // 失敗時のみタイムアウト処理
  });
}
```

## 設計方針

DC close 成功時も `closing_timeout_timer_` による WS close のタイムアウト保護を追加する。

## 完了条件

- DC+WS パスで切断がタイムアウトしてもハングしないこと
- `src/sora_signaling.cpp:669-727` の DC close 成功パスに `closing_timeout_timer_` による WS close のタイムアウト保護が追加されていること
- `CHANGES.md` の `## develop` 配下、`### misc` セクションに `[FIX]` エントリを追記する:
  ```
  - [FIX] DoInternalDisconnect DC+WS パスで WebSocket close にタイムアウト保護がないのを修正する
    - @<担当者>
  ```
