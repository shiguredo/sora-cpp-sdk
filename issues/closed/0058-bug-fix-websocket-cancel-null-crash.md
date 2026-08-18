# 切断タイマーが null の ws_ に対して Cancel() を呼び SIGSEGV でクラッシュする

- Created: 2026-08-18
- Completed: 2026-08-18
- Branch: feature/fix-websocket-cancel-null-crash
- Polished: 2026-08-18
- Reporter: @voluntas

## 目的

sora-python-sdk 経由の E2E テスト (DataChannel シグナリング有効 + パケロス環境で複数接続の確立・切断が重なる状況) で、I/O スレッドが `sora::Websocket::Cancel` の先頭で SIGSEGV になるクラッシュが発生した。

原因は `SoraSignaling` の切断処理のハンドラが実行時点の `ws_` を無検査で `Cancel()` することと、`SendOnDisconnect` → `Clear()` による `ws_ = nullptr` の競合。null の `shared_ptr` に対する `ws_->Cancel()` は `operator->` では落ちず、`this == nullptr` のまま `Websocket::Cancel` に入って最初のメンバアクセス (インライン化された `IsSSL()` の `https_proxy_` 読み出し) で null 近傍アクセスになる。クラッシュフレームが `Websocket::Cancel` の先頭に出る実際のクラッシュと整合する。

## 現状

### 1. DoInternalDisconnect のタイマー / DC close ハンドラが ws_ を無検査で Cancel() する

`src/sora_signaling.cpp` の `SoraSignaling::DoInternalDisconnect` に、`self->ws_->Cancel()` を null チェックなしで呼ぶハンドラが 3 箇所ある。

- `using_datachannel_ && ws_connected_` パス: `dc_->Close` の完了コールバックが成功分岐で張る `closing_timeout_timer_` のハンドラ
- 同パス: `dc_->Close` の完了コールバックのエラー (DC 切断タイムアウト) 分岐
- `!using_datachannel_ && ws_connected_` パス: `closing_timeout_timer_` のハンドラ

いずれも `Websocket` の `shared_ptr` をキャプチャしておらず、実行時点の `self->ws_` を参照する。一方 `SoraSignaling::SendOnDisconnect` が `io_context` に post するラムダは `Clear()` を呼び、`Clear()` は `ws_ = nullptr` と `state_ = State::Closed` への遷移を行う。`Clear()` 後にこれらのハンドラが実行されると、null の `ws_` に対して `Cancel()` が呼ばれる。

### 2. クラッシュに至る主経路 (単一 I/O スレッドで成立)

DC + WS パスでは、WebSocket 切断の完了が DataChannel の kClosed 通知より先に処理されると、次の順序で SIGSEGV に至る。post の順序が偶然入れ替わるといった特別なタイミング条件は不要で、通知の到着順だけで成立する。

1. `DoInternalDisconnect` の `using_datachannel_ && ws_connected_` パスが `dc_->Close` に完了コールバックを登録し、disconnect メッセージを送る
2. WS 側の切断が先に完了し、`OnRead` のエラー分岐 → `on_ws_close_` → `SendOnDisconnect` → post されたラムダの `Clear()` で `ws_ = nullptr`・`state_ = State::Closed` になる。`Clear()` は `dc_ = nullptr` にするが、`DataChannel` オブジェクト自体が保持する `on_close_` (= 手順 1 の完了コールバック) は消さない
3. `Clear()` の `pc_ = nullptr` で PeerConnection が解放されて DataChannel が閉じ、`DataChannel::OnStateChange` (`src/data_channel.cpp`) が全チャネルの kClosed を検出して `on_close_` を成功の `error_code` で呼ぶ
4. 完了コールバックの成功分岐は `ws_close_called` を確認せず無条件に `closing_timeout_timer_` を張り直す。`Clear()` によるタイマーの cancel は張り直しより前に済んでいるため効かず、`state_` は既に `Closed` なので以後このタイマーを cancel する者はいない
5. タイマーが満了し、ハンドラが `self->ws_->Cancel()` を null の `ws_` に対して実行して SIGSEGV になる

パケロスで close 完了の順序が揺れるほどこの経路に入りやすい。他の 2 箇所 (DC 切断タイムアウト分岐・`!using_datachannel_ && ws_connected_` パス) は現時点で具体的な null 到達順序を示せていないが、同型の無検査参照であり、同じ方針でまとめて防御する。

### 3. Websocket::Cancel 自体に null ガードがない

`src/websocket.cpp` の `Websocket::Cancel` は `IsSSL()` の結果だけで `wss_` / `ws_` を参照する。`IsSSL()` は `https_proxy_ || wss_ != nullptr` であり、HTTPS プロキシ構成では CONNECT 完了後の `OnReadProxy` で初めて `wss_` が生成されるため、それ以前に `Cancel()` が呼ばれると `IsSSL()` が true のまま null の `wss_` を参照して落ちる。

なお `Websocket` 内部の非同期ハンドラが raw `this` を使っている点 (`enable_shared_from_this` でない) は設計として脆いが、現行の `Websocket::Close` の呼び出し側 (`SoraSignaling::Redirect` と `SoraSignaling::OnConnect`) はいずれも `on_close` が `Websocket` の `shared_ptr` をキャプチャしており、pending 中に破棄される実害経路は存在しないため本 issue の範囲外とする。

## 設計方針

null になり得る実行時点の `self->ws_` をハンドラから参照するのをやめ、`DoInternalDisconnect` 実行時点 (`ws_connected_` の確認により `ws_` が非 null であることが保証されている) の `Websocket` を `ws = ws_` として `shared_ptr` でキャプチャし、それに対して `Cancel()` を呼ぶ。

DC + WS パスの 2 箇所は `dc_->Close` に渡す完了コールバックの内側にあるため、キャプチャは外側の完了コールバックのキャプチャリストで行い、内側の `closing_timeout_timer_` ハンドラとエラー分岐はその `ws` を引き継いで使う。DC close 完了時点で `self->ws_` を取り直してはならない (主経路では `Clear()` 済みで null になっているため)。

これで null 参照は構造的に消える。`Clear()` 後にハンドラが実行された場合はキャプチャ済みの生きた `Websocket` への `Cancel()` になり、切断済みソケットへの `cancel(ec)` は無害な no-op 相当で済む。

あわせて `Websocket::Cancel` 側にも防御として null ガードを入れ、`wss_` / `ws_` が null なら何もしないようにする。null の `this` で呼ばれた場合はガード自体がメンバアクセスになり防御にならないため、これはキャプチャ修正の補助であり単体では完了条件を満たさない。

## 完了条件

- `Clear()` で `ws_ = nullptr` になった後に `closing_timeout_timer_` / DC close のハンドラが実行されても SIGSEGV が発生しない (主経路の手順 1〜5 を含む)
- HTTPS プロキシ構成で `wss_` 生成前に `Cancel()` が呼ばれても落ちない
- 既存のビルドが通り、既存の E2E テストが通る

## 解決方法

- `src/sora_signaling.cpp` の `SoraSignaling::DoInternalDisconnect`:
  - `using_datachannel_ && ws_connected_` パス: `dc_->Close` に渡す完了コールバックのキャプチャリストに `ws = ws_` を追加し、成功分岐で張る `closing_timeout_timer_` のハンドラとエラー分岐の `self->ws_->Cancel()` を `ws->Cancel()` に置き換える
  - `!using_datachannel_ && ws_connected_` パス: `closing_timeout_timer_` のハンドラのキャプチャリストに `ws = ws_` を追加し、`self->ws_->Cancel()` を `ws->Cancel()` に置き換える
- `src/websocket.cpp` の `Websocket::Cancel`: `IsSSL()` 分岐の前に `wss_` / `ws_` の null チェックを追加し、null なら何もしない

タイミング依存の競合のため決定的な再現テストは書けない (モック禁止)。回帰確認は既存の E2E テストで行う。

### 実施内容

上記の解決方法のとおり実装した。無検査の `self->ws_->Cancel()` とタイマーの張り直しを導入したのはコミット aabba857 (2026.2.0-canary.22 初出) である。

検証は、DC の kClosed の post が Clear() の post より後ろに並ぶ順序を、`on_ws_close_` が Clear() を post した直後に I/O スレッドを一時的に塞ぐ再現ハーネスで決定的に再現して行った。修正前は null の `this` で `Websocket::Cancel` に突入すること、修正後は同一順序でも捕捉済みの生きた `Websocket` への `Cancel()` になりクラッシュしないこと、`Cancel()` 実行後に捕捉していた最後の参照の解放で `Websocket` が破棄されること (リークなし) をログで確認した。通常順序の切断も既存挙動どおり動作することを確認した。
