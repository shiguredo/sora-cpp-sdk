# DoInternalDisconnect DC+WS パスで DC close 成功後に WS close が来ないと切断がハングする

- Priority: High
- Created: 2026-07-10
- Polished: 2026-07-10

## 目的

`DoInternalDisconnect` の `using_datachannel_ && ws_connected_` パスでは DC の切断タイムアウト （`disconnect_wait_timeout`, デフォルト 5 秒） があることを理由に、WS 側の `closing_timeout_timer_` を使用していない。しかし DC と WS のタイムアウトは独立した保護であり、一方が他方を代替できない。

DC `Close()` コールバック （`src/sora_signaling.cpp:700-704`） は成功時 （`!ec`） に早期 return し何もしない。この後は `on_ws_close_` コールバックだけが切断完了のトリガーになる。サーバが DC close 後に WebSocket を閉じない場合や、ネットワーク分断で WS close フレームが到達しない場合、`on_ws_close_` は永遠に呼ばれず切断が完了しない。これによりアプリケーションがハングし、プロセスの正常終了が不可能になる。

## 優先度根拠

DC シグナリング （`using_datachannel_ = true`） かつ WS 併用 （`ws_connected_ = true`） の全ユーザが対象。切断シーケンスがハングすると `Disconnect()` → `SendOnDisconnect` が一切呼ばれず、アプリケーションは切断を検知できない。プロセス終了時に強制 kill が必要になり、リソース解放も行われない。切断パスの根本的な欠陥であるため High。

## 現状

`src/sora_signaling.cpp:669-704` （問題箇所に絞った抜粋）:

```cpp
if (using_datachannel_ && ws_connected_) {
  // DC の切断タイムアウトがあるので、closing_timeout_timer_ は使わない  // ← 誤った前提
  std::shared_ptr<bool> ws_close_called = std::make_shared<bool>(false);
  std::shared_ptr<bool> dc_close_called = std::make_shared<bool>(false);
  on_ws_close_ = [self = shared_from_this(), on_close, ws_close_called,
                  dc_close_called](boost::system::error_code ec) {
    // 既に DC 側で処理済み
    if (*dc_close_called) {
      return;
    }
    *ws_close_called = true;
    auto reason = self->ws_->reason();
    self->SendOnWsClose(reason);
    if (ec != boost::beast::websocket::error::closed) {
      // ... CLOSE_FAILED ...
    } else {
      // ... CLOSE_SUCCEEDED ...
    }
  };

  dc_->Close(
      disconnect,
      [self = shared_from_this(), on_close, ws_close_called,
       dc_close_called](boost::system::error_code ec) {
        // 正常な切断なら何もしない
        if (!ec) {
          return;  // ← ここで早期 return。WS close の待ち受けが放棄される
        }
        // ... 失敗時のみタイムアウト処理 ...
      },
      config_.disconnect_wait_timeout);
}
```

対比として、非 DC WS パス （`src/sora_signaling.cpp:749-758`） では以下のように `closing_timeout_timer_` が正しく使われている:

```cpp
closing_timeout_timer_.expires_after(
    std::chrono::seconds(config_.websocket_close_timeout));
closing_timeout_timer_.async_wait(
    [self = shared_from_this()](boost::system::error_code ec) {
      if (ec) { return; }
      self->ws_->Cancel();
    });
on_ws_close_ = [self = shared_from_this(), on_close](boost::system::error_code ec) {
  self->closing_timeout_timer_.cancel();  // ← on_ws_close_ 内でタイマーキャンセル
  auto reason = self->ws_->reason();
  if (ec == boost::asio::error::operation_aborted) {  // ← タイムアウト時の分岐
    auto timeout_reason = boost::beast::websocket::close_reason(
        (boost::beast::websocket::close_code)4999, "DISCONNECT-WAIT-TIMEOUT-ERROR");
    self->SendOnWsClose(timeout_reason);
  } else {
    self->SendOnWsClose(reason);
  }
  // ...
};
```

## 設計方針

DC close 成功時に `closing_timeout_timer_` による WS close のタイムアウト保護を追加する。DC の切断タイムアウトと WS の切断タイムアウトは独立した保護であり、DC 成功後も WS close を待つ必要があるため。非 DC パスと同一のパターン （timer → `ws_->Cancel()` → `on_ws_close_` 経由で切断完了） を踏襲する。

### エッジケース

- **DC close 成功 → タイマー発火より先にサーバが WS を正常 close**: `on_ws_close_` 内の `closing_timeout_timer_.cancel()` でタイマーがキャンセルされ、`CLOSE_SUCCEEDED` で正常終了する
- **DC close 成功 → タイマー発火 （WS close 未到達）**: `ws_->Cancel()` → `on_ws_close_` （`operation_aborted`） → `close_reason(4999)` → `CLOSE_FAILED` で切断完了する
- **DC close 成功 → タイマー発火とサーバからの WS close が同時に発生**: `*ws_close_called` チェックにより先着のみ `on_close` が呼ばれ、後着は return する。`*dc_close_called` のみのチェックでは DC close 成功パスで `*dc_close_called` が `false` のままであるため `on_ws_close_` の二重回入を防止できず、`*ws_close_called` の追加チェックが必須
- **DC close 失敗 （タイムアウト）**: 既存のエラーパスにより `ws_->Cancel()` が呼ばれ切断完了する。本修正のタイマーは開始されない

### 後方互換性

正常系では WS close は即座に返るためタイムアウトは発生しない。異常系では最大 `config_.websocket_close_timeout` （デフォルト 3 秒） のタイムアウトにより `CLOSE_FAILED` が返る。

## 完了条件

- DC close 成功後、サーバが WS を閉じない場合でも `config_.websocket_close_timeout` 秒後に `OnDisconnect` が `SoraSignalingErrorCode::CLOSE_FAILED` で呼ばれること
- DC close 成功後、サーバが WS を正常に閉じた場合は `OnDisconnect` が `SoraSignalingErrorCode::CLOSE_SUCCEEDED` で呼ばれること （既存動作のリグレッションがないこと）
- DC close 失敗 （タイムアウト） 時は従来通り DC 側のエラーパスで切断が完了し、本修正によるタイマーが干渉しないこと
- `CHANGES.md` の `## develop` 配下 （`### misc` より前） に `[FIX]` エントリを追記する:
  ```
  - [FIX] DoInternalDisconnect DC+WS パスで DC close 成功後に WS close が来ないと切断がハングするのを修正する
    - @<担当者>
  ```

## 解決方法

`src/sora_signaling.cpp` の `DoInternalDisconnect` 内、`using_datachannel_ && ws_connected_` パスを以下のように修正する。

### 変更 1: DC close 成功時にタイマーを開始する

`dc_->Close()` のコールバック内、703-704 行目の `if (!ec) { return; }` を以下のように置き換える:

```cpp
// DC close 成功後も WS close を待つ必要があるため closing_timeout_timer_ で保護する
if (!ec) {
  self->closing_timeout_timer_.expires_after(
      std::chrono::seconds(self->config_.websocket_close_timeout));
  self->closing_timeout_timer_.async_wait(
      [self](boost::system::error_code ec) {
        if (ec) {
          return;
        }
        self->ws_->Cancel();
      });
  return;
}
```

### 変更 2: on_ws_close_ にタイマーキャンセルと operation_aborted ハンドリング、ws_close_called チェックを追加する

`on_ws_close_` ラムダ （673-691 行目） を以下のように置き換える。（変更部分のみにコメントを付与）:

```cpp
on_ws_close_ = [self = shared_from_this(), on_close, ws_close_called,
                dc_close_called](boost::system::error_code ec) {
  // 追加: タイマーをキャンセルする
  self->closing_timeout_timer_.cancel();

  // 変更: DC 側で処理済み、または既にこのコールバックが処理済みなら何もしない
  if (*dc_close_called || *ws_close_called) {
    return;
  }
  *ws_close_called = true;

  // 追加: タイムアウト時は reason を自作する
  boost::beast::websocket::close_reason reason;
  if (ec == boost::asio::error::operation_aborted) {
    reason = boost::beast::websocket::close_reason(
        (boost::beast::websocket::close_code)4999,
        "DISCONNECT-WAIT-TIMEOUT-ERROR");
  } else {
    reason = self->ws_->reason();
  }
  self->SendOnWsClose(reason);

  if (ec != boost::beast::websocket::error::closed) {
    std::string message = "Failed to close WebSocket: ec=" + ec.message() +
                          " wscode=" + std::to_string(reason.code) +
                          " wsreason=" + reason.reason.c_str();
    on_close(false, SoraSignalingErrorCode::CLOSE_FAILED, message);
  } else {
    on_close(true, SoraSignalingErrorCode::CLOSE_SUCCEEDED,
             "Succeeded to close Websocket (DC signaling is enabled)");
  }
};
```

### 変更 3: コメントを更新する

670 行目のコメントを新たな設計に合わせて更新する:

```cpp
// DC の切断には disconnect_wait_timeout を、
// DC 成功後の WS close には closing_timeout_timer_ をそれぞれ使う
```

### テスト戦略

本修正はタイマーと切断シーケンスの競合に関わるため、以下の方法で検証する:

- **手動検証**: `websocket_close_timeout` を 1 秒に設定し、DC シグナリング有効状態で接続後、サーバ側で WS を閉じないケース（TCP RST やサーバ強制終了等） を再現して `OnDisconnect(CLOSE_FAILED)` がタイムアウト後に呼ばれることを確認する
- **E2E テスト**: sumomo を用い、`websocket_close_timeout` を短く設定して切断シーケンスがハングせず完了することを検証する。正常系リグレッション （`OnDisconnect(CLOSE_SUCCEEDED)`） も E2E テストでカバーする。詳細はテスト実装時に検討する

### 変更対象ファイル

- `src/sora_signaling.cpp` （`DoInternalDisconnect` 関数内、669-727 行目）
- `CHANGES.md` （`## develop` 配下、`### misc` より前に `[FIX]` エントリを追記）
