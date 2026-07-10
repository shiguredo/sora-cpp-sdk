# Disconnect() during Init で OnDisconnect 未呼び出し・Clear() 未実行

- Priority: Medium
- Created: 2026-07-10
- Polished: 2026-07-10

## 目的

`Disconnect()` が `Init` 状態で呼ばれた場合、`state_ = State::Closed` を設定するだけで `SendOnDisconnect` も `Clear()` も呼ばない。`Connecting` での `Disconnect` は `SendOnDisconnect` + `Clear` を呼ぶため非対称。`Clear()` 未実行のため `connection_timeout_timer_` や `connecting_wss_` が残留し、次の `Connect()` で予期しない状態になる可能性がある。

## 優先度根拠

`Init` 状態での切断は通常のユースケースではないが、非対称な挙動はバグの温床となる。Medium。

## 現状

`src/sora_signaling.cpp:178-180`:

```cpp
void SoraSignaling::Disconnect() {
  if (state_ == State::Init) {
    state_ = State::Closed;
    return;  // SendOnDisconnect も Clear() も呼ばれない
  }
  // ...
}
```

## 設計方針

`Init` 状態でも `Clear()` を呼び、`Connecting` と一貫したクリーンアップを行う。`OnDisconnect` を呼ぶかどうかは設計判断（`Connect()` 前に `Disconnect()` を呼んだユーザーに通知すべきか）。

## 完了条件

- `Init` 状態の `Disconnect()` でも `Clear()` が呼ばれること
- `src/sora_signaling.cpp:178-180` の `Init` 状態処理で `Clear()` が実行されること
- `CHANGES.md` の `## develop` 配下、`### misc` セクションに `[FIX]` エントリを追記する:
  ```
  - [FIX] Disconnect() during Init で OnDisconnect 未呼び出し・Clear() 未実行を修正する
    - @<担当者>
  ```
