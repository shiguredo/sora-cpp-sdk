# DataChannel::Close の on_close 二重呼び出しとデストラクタ UAF を修正する

- Priority: High
- Created: 2026-07-10
- Completed: 2026-07-14
- Model: DeepSeek V4 Pro
- Branch: feature/fix-datachannel-double-callback
- Polished: 2026-07-10
## 目的

`src/data_channel.cpp` の切断シーケンスに以下の 2 つのバグが存在する:

1. **on_close 二重呼び出し**: `Close()` のタイマーラムダ (`[on_close]` を値キャプチャ) が発火時に `on_close(timed_out)` を呼び出すが、メンバ `on_close_` を `nullptr` にクリアしない。このため後続の `OnStateChange` が `on_close_` を読み取り、`on_close(success)` を再呼び出しする。
2. **デストラクタ UAF**: `~DataChannel()` が Thunk の observer 登録を解除 (`UnregisterObserver`) していない。`DataChannel` 破棄後に WebRTC が Thunk をコールバックすると、生ポインタ `Thunk::p` 経由で解放済みの `DataChannel` にアクセスし UAF が発生する。

## 優先度根拠

切断シーケンスでユーザーコールバックが二重に呼ばれ、アプリケーションの状態が破綻する。UAF はクラッシュに直結。共に High。

## 現状

### on_close 二重呼び出し

`src/data_channel.cpp:78-91` （`Close()` 全体）のタイマー設定部。ラムダは `[on_close]` のみをキャプチャし `this` をキャプチャしていないため、`on_close_` メンバをクリアできない:

```cpp
timer_.expires_after(
    std::chrono::milliseconds((int)(disconnect_wait_timeout * 1000)));
timer_.async_wait([on_close](boost::system::error_code ec) {
  if (ec == boost::asio::error::operation_aborted) {
    return;
  }
  on_close(
      boost::system::errc::make_error_code(boost::system::errc::timed_out));
  // on_close_ がクリアされない
});

on_close_ = on_close;
```

`src/data_channel.cpp:113-148` （`OnStateChange`）には既にガードが存在する — `on_close_` を `nullptr` にクリアし `timer_.cancel()` を呼んでいる。しかしタイマーが **先に** 発火した場合、`on_close_` はクリアされず、後続の `OnStateChange` が以下のコードを通過して二回目のコールバックを実行する:

```cpp
auto on_close = self->on_close_;          // コピー（非 null）
auto empty = self->thunks_.empty();
if (on_close != nullptr && empty) {
  self->on_close_ = nullptr;
  self->timer_.cancel();
}
// ... observer コールバック ...
if (on_close != nullptr && empty) {
  on_close(boost::system::error_code());  // 二度目のコールバック
}
```

二重呼び出しが発生するのは「タイマーが先に発火 → その後で OnStateChange が発火」の順序のみ。「OnStateChange が先」の場合は `timer_.cancel()` によりタイマーが `operation_aborted` で早期 return するため安全である。

### デストラクタ UAF

`src/data_channel.cpp:38-40` のデストラクタはログ出力のみで、Thunk の observer 解除を行っていない:

```cpp
DataChannel::~DataChannel() {
  RTC_LOG(LS_INFO) << "dtor DataChannel";
}
```

`Thunk::p` は生ポインタ (`include/sora/data_channel.h:35`):

```cpp
struct Thunk : webrtc::DataChannelObserver,
               std::enable_shared_from_this<Thunk> {
    DataChannel* p;  // 生ポインタ
    ...
};
```

また `AddDataChannel` (`src/data_channel.cpp:103`) で `data_channel->RegisterObserver(thunk.get())` により WebRTC 側に生ポインタが登録されている。`DataChannel` 破棄時に `thunks_` の `shared_ptr<Thunk>` が解放され Thunk が破棄されるが、WebRTC の `DataChannelInterface` が生存している場合、登録された生ポインタ経由で Thunk のコールバックが呼ばれ UAF となる。

## 設計方針

### on_close 二重呼び出し修正

タイマーラムダに `weak_from_this()` をキャプチャさせ、`on_close` 呼び出し後に `lock()` 成功時のみ `on_close_ = nullptr` を設定する。正常系では `on_close` コールバックが `shared_ptr<SoraSignaling>` → `dc_` → `shared_ptr<DataChannel>` の連鎖で生存を保証しているため `lock()` は成功する。異常系で DataChannel が既に破棄済みの場合は `lock()` が `nullptr` を返し安全にスキップされる。`shared_from_this()` と異なり不要な延命を発生させない。`OnStateChange` には既にガードが存在するため追加の修正は不要。

### デストラクタ UAF 修正

デストラクタで `thunks_` をイテレートし、各 Thunk の `dc` に対して `UnregisterObserver()` を呼ぶ。通常の切断フローでは `OnStateChange` 内で各 DC の observer が解除されるため、デストラクタのループは空回りする。デストラクタの解除は `Close()` を経由しない異常系（`DataChannel` の直接破棄）に対する防御である。

### スレッド安全性の前提

`DataChannel` の全操作（`Close()`, `OnStateChange`, `~DataChannel()`）は単一の `io_context` スレッド上で直列実行される。`SoraSignaling` はすべての処理を `boost::asio::post` 経由で同一の `io_context` に post している。本修正はこの前提の上に成り立つ。

### 後方互換性

修正はすべて `src/data_channel.cpp` 内部に閉じ、ヘッダ変更・公開 API への影響はない。既存の切断シーケンスにおける正常系の動作は変わらない。

### エッジケース

| ケース | 修正前の挙動 | 修正後の挙動 |
|---|---|---|
| OnStateChange → タイマー | `timer_.cancel()` によりタイマーは `operation_aborted` で return。安全 | 変更なし。`on_close_` が `nullptr` クリア済みのためタイマー内のガードも通過 |
| タイマー → OnStateChange | タイマーが `on_close(timed_out)` を呼び、OnStateChange が `on_close(success)` を再呼び出し | タイマーが `on_close_ = nullptr` を設定済みのため、OnStateChange は二重回入しない |
| `signaling` ラベル不在 | `on_close(error)` が同期的に呼ばれ早期 return。タイマーも `on_close_` も設定されない | 変更なし |
| `Close()` 複数回呼び出し | タイマーが上書きされ、`on_close_` も上書きされる | 変更なし。1 回目の `on_close` が破棄されるのは既存動作 |
| `Close()` と `SetOnClose()` の競合 | `on_close_` が後勝ちで上書きされる | 変更なし。両方が同時に使われるシナリオは現実的に存在しない |
| `DataChannel` がタイマー発火前に外部から破棄される | タイマー完了ハンドラが `operation_aborted` で return するため安全 | タイマーが `operation_aborted` で早期 return するか、正常発火時も `weak_from_this().lock()` が `nullptr` を返し `on_close_` へのアクセスをスキップする。`shared_from_this()` と異なり不要な延命を発生させない |
| Thunk が 0 個の状態で `OnStateChange` | `thunks_.find` で早期 return | 変更なし |

## 完了条件

- `Close()` で `on_close` が最大 1 回しか呼ばれないこと
- デストラクタで Thunk の observer が適切に解除されること
- タイミング依存の二重呼び出しであり単体テストでの再現は困難である。`test/` 配下に `DataChannel` を対象とした既存テストはないため、以下の方法で検証する:
  - **コードレビュー**: 修正後のコードがエッジケース表の全パターンを満たすことを確認
  - **既存 E2E テスト**: sumomo を用いた切断シーケンスで退行がないことを確認
  - **AddressSanitizer (ASan)**: CMake ビルド時に `-DCMAKE_CXX_FLAGS="-fsanitize=address"` を追加してビルドし、切断シーケンス実行時に heap-use-after-free が検出されないことを確認
    - 実行時に `ASAN_OPTIONS=detect_container_overflow=0` を設定すること（libwebrtc の内部実装がトリガーする擬陽性を除外するため）
- `CHANGES.md` に `[FIX]` エントリが追記されていること

## 解決方法

### 変更 1: タイマーラムダに `weak_from_this()` をキャプチャさせ `on_close_` をクリアする

`src/data_channel.cpp:80-86` のタイマーラムダを以下のように修正する:

```cpp
timer_.async_wait([on_close, wself = weak_from_this()](boost::system::error_code ec) {
  if (ec == boost::asio::error::operation_aborted) {
    return;
  }
  on_close(
      boost::system::errc::make_error_code(boost::system::errc::timed_out));
  if (auto self = wself.lock()) {
    self->on_close_ = nullptr;
  }
});
```

`weak_from_this()` をキャプチャすることで、正常系では `on_close` コールバック経由の生存連鎖により `lock()` が成功し `on_close_` を安全にクリアできる。異常系では `lock()` が `nullptr` を返すため UAF を回避しつつ、`shared_from_this()` のような不要な延命も発生させない。

### 変更 2: デストラクタで Thunk observer を解除する

`src/data_channel.cpp:38-40` のデストラクタを以下のように修正する:

```cpp
DataChannel::~DataChannel() {
  RTC_LOG(LS_INFO) << "dtor DataChannel";
  for (auto& [thunk, dc] : thunks_) {
    dc->UnregisterObserver();
  }
}
```

通常の切断フローでは `OnStateChange` (`src/data_channel.cpp:128`) が各 DC の observer を解除済みのため、デストラクタ内のループは 0 回で終了する。本修正は異常系（`Close()` を経由せず `DataChannel` が破棄されるケース）に対する防御である。

### 変更 3: CHANGES.md に追記する

`CHANGES.md` の `## develop` 直下（`### misc` セクションより前）に以下を追記する:

```
- [FIX] DataChannel::Close の on_close 二重呼び出しとデストラクタ UAF を修正する
  - @<担当者>
```

### 変更対象ファイル

- `src/data_channel.cpp` （`Close()` のタイマーラムダ、`~DataChannel()` の 2 箇所）
- `CHANGES.md`（`## develop` 直下に `[FIX]` エントリを追記）
