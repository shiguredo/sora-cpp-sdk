# DataChannel::Close でタイマーと OnStateChange が on_close を二重呼び出し

- Priority: High
- Created: 2026-07-10
- Completed: {YYYY-MM-DD}
- Model: DeepSeek V4 Pro
- Branch: feature/fix-datachannel-double-callback
- Polished: {YYYY-MM-DD}

## 目的

`src/data_channel.cpp` の `Close()` でタイマーコールバックと `OnStateChange` の両方が `on_close` を呼び出す可能性がある。タイマーが先に発火した場合、`on_close(timed_out)` が呼ばれた後、`OnStateChange` から `on_close(success)` が再度呼ばれ二重コールバックとなる。また `DataChannel` デストラクタが Thunk の observer を解除しないため UAF の可能性もある。

## 優先度根拠

切断シーケンスでユーザーコールバックが二重に呼ばれ、アプリケーションの状態が破綻する。UAF はクラッシュに直結。High。

## 現状

`src/data_channel.cpp:80-86`（タイマーラムダ）:

```cpp
timer_.expires_after(std::chrono::duration<double>(disconnect_wait_timeout));
timer_.async_wait([on_close, this](boost::system::error_code ec) {
    if (ec) return;
    on_close(make_error_code(boost::system::errc::timed_out));
    // on_close_ がクリアされない
});
```

`src/data_channel.cpp:134-146`（OnStateChange）:

```cpp
auto on_close = std::move(self->on_close_);  // まだセットされている
on_close(boost::system::error_code());       // 二度目のコールバック
```

タイマー発火後に `on_close_` が nullptr にクリアされないため、`OnStateChange` が後から呼ばれた場合に二重呼び出しが発生する。

## 設計方針

タイマー発火時に `on_close_ = nullptr` を設定し、`OnStateChange` 側のガードを強化する。デストラクタで Thunk observer を解除する。

## 完了条件

- `Close()` で `on_close` が最大 1 回しか呼ばれないこと
- デストラクタで Thunk の observer が適切に解除されること
