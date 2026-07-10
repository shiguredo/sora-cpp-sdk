# SendOnDisconnect にガードがなく OnDisconnect が二重通知される問題

- Priority: High
- Created: 2026-07-10
- Completed: {YYYY-MM-DD}
- Model: DeepSeek V4 Pro
- Branch: feature/fix-send-on-disconnect-double-notify
- Polished: 2026-07-10
## 目的

`SendOnDisconnect()` は `state_` をチェックせず、呼ばれるたびに無条件で `Clear()` + `OnDisconnect` を post する。`SoraSignaling` の処理はすべて単一の `io_context` に post して直列実行されるため、これはスレッド競合 (data race) ではない。しかし同一の `io_context` キューに `SendOnDisconnect` の post が複数積まれると、それぞれの post ラムダが `OnDisconnect` を呼び出し、`OnDisconnect` が複数回通知されてしまう。

以下の経路で `OnDisconnect` が 2 回以上呼ばれうる:

1. `Connecting` 中に `connection_timeout_timer_` が発火して `SendOnDisconnect` (`src/sora_signaling.cpp:1245`) が post され、その前後で `Disconnect()` の `Connecting` 分岐 (`src/sora_signaling.cpp:183`) が `SendOnDisconnect` を post する。`Disconnect()` は `Connecting` 分岐でタイマを cancel しないため、両方の post が実行される
2. `Connecting` 中に `connection_timeout_timer_` が発火して `SendOnDisconnect` (`src/sora_signaling.cpp:1245`) が post され、`OnConnect` の全接続失敗パス (`src/sora_signaling.cpp:836`) も `SendOnDisconnect` を post する

`SendOnDisconnect` の呼び出し元は他にも `Redirect` のタイマ (`src/sora_signaling.cpp:255`)、`DoConnect` の URL 不正時 (`src/sora_signaling.cpp:1292`)、`PeerConnectionState::kFailed` (`src/sora_signaling.cpp:1541`)、`DoInternalDisconnect` (`src/sora_signaling.cpp:662,664`) など複数あり、いずれも同一経路で重複通知が起こりうる。ガードを 1 箇所（`SendOnDisconnect` の post ラムダ内）に入れることで、すべての呼び出し元の組み合わせをまとめて防御する。

## 優先度根拠

切断通知が複数回届くとアプリケーションの状態管理が破綻する。`Disconnect()` の `Connecting` 分岐は `CLOSE_SUCCEEDED` を使うため `SendOnDisconnect` 内のエラーログ (`src/sora_signaling.cpp:1417-1418`) にも現れず、重複通知が起きてもログ上に異常が出ないため検知が難しい。タイミング依存で再現が困難なバグ。High。

## 現状

`src/sora_signaling.cpp:1415-1428`:

```cpp
void SoraSignaling::SendOnDisconnect(SoraSignalingErrorCode ec,
                                     std::string message) {
  if (ec != SoraSignalingErrorCode::CLOSE_SUCCEEDED) {
    RTC_LOG(LS_ERROR) << "Failed to Disconnect: message=" << message;
  }
  // state_ をチェックせず無条件に post する
  boost::asio::post(*config_.io_context, [self = shared_from_this(), ec,
                                          message = std::move(message)]() {
    self->Clear();
    auto ob = self->config_.observer.lock();
    if (ob != nullptr) {
      ob->OnDisconnect(ec, std::move(message));
    }
  });
}
```

`Clear()` (`src/sora_signaling.cpp:1474-1493`) の末尾で `state_ = State::Closed` (`src/sora_signaling.cpp:1492`) が設定される。`Clear()` はべき等（タイマ cancel や `nullptr` 代入は複数回呼んでも安全）なので、問題の本質は `Clear()` の重複ではなく `OnDisconnect` オブザーバコールバックが複数回呼ばれることにある。

## 設計方針

`SendOnDisconnect` の **post ラムダ内、`Clear()` 呼び出しの前** に、`state_` がすでに `Closed` なら return するガードを追加する。

ガードを post **前**の同期コンテキストに置いてはならない。`state_` が `Closed` になるのは post ラムダ内で実行される `Clear()` の中 (`src/sora_signaling.cpp:1492`) であり、複数の `SendOnDisconnect` が連続して呼ばれた時点では `state_` はまだ `Connecting` などのままである。post 前にガードを置いても両方の post が実行され、二重通知を防げない。1 個目の post ラムダが `Clear()` で `state_ = Closed` にした後、2 個目以降の post ラムダがガードで return する形にする必要がある。

```cpp
void SoraSignaling::SendOnDisconnect(SoraSignalingErrorCode ec,
                                     std::string message) {
  if (ec != SoraSignalingErrorCode::CLOSE_SUCCEEDED) {
    RTC_LOG(LS_ERROR) << "Failed to Disconnect: message=" << message;
  }
  boost::asio::post(*config_.io_context, [self = shared_from_this(), ec,
                                          message = std::move(message)]() {
    // 先行する SendOnDisconnect の post ラムダで既に Clear() が呼ばれ
    // state_ == Closed になっている場合、二重通知になるため return する
    if (self->state_ == State::Closed) {
      return;
    }
    self->Clear();
    auto ob = self->config_.observer.lock();
    if (ob != nullptr) {
      ob->OnDisconnect(ec, std::move(message));
    }
  });
}
```

`state_ == Closed` ガードを採用する理由:

- `state_` を `Closed` に設定するのは `Clear()` (`src/sora_signaling.cpp:1492`) のみであり、`Clear()` は `OnDisconnect` 通知とペアで呼ばれる。「`Closed` = 既に切断通知済み」とみなせる
- 既存の状態管理 (`state_`) をそのまま使うため、新規メンバ変数の追加が不要
- `disconnect_sent_` のような専用フラグを導入する案もあるが、ヘッダ (`include/sora/sora_signaling.h`) へのメンバ追加と `Clear()` でのリセット判断が必要になり、状態管理が二重化する。`state_` ガードの方が最小修正で済む

複数の post が積まれた場合、最初に実行された post ラムダの `ec` / `message` で `OnDisconnect` が通知され、後続はガードで抑制される（どの post が最初になるかはキュー順で決まる）。この「最初の通知が採用される」挙動は許容する。

## 完了条件

- 切断シーケンス全体で `OnDisconnect` が最大 1 回しか呼ばれないこと
- 設計方針のとおり、`SendOnDisconnect` の post ラムダ内かつ `Clear()` 呼び出しの前に `if (state_ == State::Closed) return;` のガードが追加されていること
- 正常系（単一の切断）で `OnDisconnect` が従来どおり 1 回呼ばれることを確認すること
- タイミング依存の重複通知であり単体テストでの再現は困難である。`test/` 配下に `SendOnDisconnect` を対象とした既存テストはないため、既存の E2E テストまたは手動での切断シーケンス確認で退行がないことを確認する
- `CHANGES.md` の `## develop` 直下（`### misc` セクションより前）に `[FIX]` エントリを追記する。`### misc` は Examples / CI / tooling 用のため使わない:
  ```
  - [FIX] SendOnDisconnect にガードがなく OnDisconnect が二重通知される問題を修正する
    - @<担当者>
  ```
