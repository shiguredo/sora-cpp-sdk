# DataChannel が閉じられた際にクライアントが切断されない問題を修正する

- Created: 2026-09-10
- Completed: 2026-09-19
- Branch: feature/fix-disconnect-on-datachannel-closed
- Polished: 2026-09-13
- Reporter: @miosakuma

## 目的

DataChannel シグナリング利用中に DataChannel が閉じられたことをクライアントが検知できるようにし、切断処理 (`OnDisconnect` の通知) を行えるようにする。

現状は DataChannel が閉じられてもクライアントは接続を維持し続ける。signaling ラベルの DataChannel が閉じると DataChannel 経由のシグナリングができなくなる。WebSocket が既に切断されている構成 (`ignore_disconnect_websocket` が true) では re-answer を送る手段がなくなり、Sora からの re-offer に応答できないまま音声・映像の送受信だけが継続する。実測では Sora 側からも切断されず、クライアント側も `OnDisconnect` を受け取らないため、アプリケーションはこの異常を検知できない。

Sora の DataChannel 仕様では signaling ラベルの DataChannel が閉じられたらクライアントは終了処理を開始することになっている。また sora-js-sdk はすべての DataChannel の close で `disconnect()` を呼び、sora-android-sdk も label を問わず切断する動作が意図的であると整理されている (一部が不意に閉じた接続は健全ではないため速やかに全体を閉じる、という方針)。本 issue では同じ挙動に揃える。

## 現状

- `src/data_channel.cpp` の `DataChannel::OnStateChange` は、DataChannel が kClosed になると `labels_` と `thunks_` から該当ラベルを削除して `DataChannelObserver::OnStateChange` を呼ぶ。`on_close_` が設定されている場合はすべての DataChannel が閉じた時点で `on_close_` を呼ぶが、設定されていなければ何もしない
- `src/sora_signaling.cpp` の `SoraSignaling::OnStateChange` は、offer の `data_channels` に含まれるラベルのうち open になったものを `OnDataChannel(label)` で通知するだけで、閉じた DataChannel を検知しても切断処理を行わない
- `on_close_` は Sora から `{"type":"close"}` を受信したときは `DataChannel::SetOnClose` で、クライアント起点の切断時は `DataChannel::Close` で設定される。それ以外の DataChannel の終了は切断として扱われない
- DataChannel が閉じても PeerConnection とメディアの送受信は継続するため、クライアントは接続が生きていると認識し続ける

### 再現手順

1. sumomo を DataChannel シグナリング有効で接続する

   ```bash
   ./sumomo --signaling-url wss://<signaling-url>/signaling --role sendrecv --channel-id <channel-id> --data-channel-signaling true --ignore-disconnect-websocket true
   ```

2. Sora のデバッグ API で `signaling` または `stats` ラベルの DataChannel を閉じる
3. sumomo は音声・映像の送受信を続けたまま切断されない。Sora からの re-offer に対して re-answer が返らない

## 設計方針

- `SoraSignaling::OnStateChange` で DataChannel の状態を確認し、kClosed になった DataChannel を検知したら切断処理を開始する
- 対象は signaling ラベルに限定せず、offer の `data_channels` に含まれるすべての DataChannel (`#` で始まるユーザー定義ラベルを含む) とする。一部が不意に閉じた接続は健全ではないため全体を閉じる、という方針に揃える
- 意図的な切断と競合させない
  - クライアント起点の切断は `DoInternalDisconnect` で `state_ = State::Closing` にしてから DataChannel を閉じるため、`state_ == State::Connected` のときだけ検知する
  - サーバ起点のグレースフルシャットダウンは `{"type":"close"}` の受信後にすべての DataChannel の close を待って通知する既存処理がある。この処理を優先させ、`{"type":"close"}` 受信後は新しい検知を抑止する (`DataChannel` に問い合わせる、`SoraSignaling` にフラグを持つ、state を `State::Closing` にするなど、実装時に最も単純な方法を選ぶ)
- 通知する `SoraSignalingErrorCode` は DataChannel の close が原因であると分かる値にする。新規に `DATACHANNEL_CLOSED` のような値を追加するか、既存の値にメッセージを付けて通知するかは実装時に決める
- `DataChannel` の `on_close_` はサーバ起点のグレースフルシャットダウン用として温存し、新しい検知とは独立させる

## 完了条件

- 接続中に offer の `data_channels` に含まれる任意の DataChannel（`signaling` や `stats` ラベル、`#` で始まるユーザー定義ラベルを含む。DataChannel シグナリングの利用中に限らない）が閉じられた場合、`OnDisconnect` が 1 回呼ばれること
- `{"type":"close"}` によるサーバ起点のグレースフルシャットダウンでは、従来どおりすべての DataChannel の close を待って通知され、二重通知や close コード・reason の喪失がないこと
- クライアント起点の `Disconnect()` で二重通知が発生しないこと
- 回帰がないこと: `test/datachannel.cpp` の DataChannel 送受信テストと、E2E テスト (`test_sumomo_data_channel_signaling`) が通ること
- DataChannel が閉じられた場合に `OnDisconnect` が呼ばれることを自動テスト (`test/datachannel_closed.cpp`) で確認すること
  - クライアントが切断処理を開始していない状態で Sora に DataChannel を閉じさせる必要があるが、そのための API は存在しないため、クライアントから signaling DataChannel へ `{"type":"disconnect"}` を送ることで同じ状況を作る
- 変更履歴 (`CHANGES.md`) の `## develop` の `[FIX]` にエントリを追記する

## 解決方法

- `src/sora_signaling.cpp` の `SoraSignaling::OnStateChange()` で DataChannel の `kClosed` を検知したら、`SoraSignalingErrorCode::DATACHANNEL_CLOSED` で `DoInternalDisconnect()` を呼ぶようにした
  - 対象は `signaling` ラベルに限定せず、offer の `data_channels` に含まれるすべての DataChannel とした
  - クライアント起点の切断と競合しないように `state_ == State::Connected` のときだけ検知する
  - サーバから `{"type":"close"}` を受信した後は `received_close_` で検知を抑止し、すべての DataChannel の close を待つ既存処理を優先する
- `SoraSignalingErrorCode` に `DATACHANNEL_CLOSED` を追加した
- `SoraSignaling::DoInternalDisconnect()` の切断方法の判定を `using_datachannel_` から `using_datachannel_ && dc_->IsOpen("signaling")` に変更した
  - signaling DataChannel が閉じられている場合は DataChannel 経由で送信できないため、WebSocket 経由の切断に切り替わる
- `test/datachannel_closed.cpp` を追加した
  - signaling DataChannel へ `{"type":"disconnect"}` を送って Sora に DataChannel を閉じさせ、`DATACHANNEL_CLOSED` で `OnDisconnect` が 1 回だけ呼ばれることを確認する
- `test/datachannel.cpp` が `OnDataChannel()` で開いたすべてのラベルにメッセージを送っていたため、ユーザー定義ラベルにのみ送るようにした
  - Sora の管理するラベルへ送信すると Sora が接続を 4490 INTERNAL-ERROR で切断する。SDK 側で送信先を制限する対応は issue 0107 として起票した
- `CHANGES.md` の `## develop` の `[FIX]` にエントリを追記した

### 確認したこと

- `test/datachannel.cpp` が通ること (10 回の接続がすべて成功)
- `test/datachannel_closed.cpp` が通ること (`DATACHANNEL_CLOSED` で `OnDisconnect` が 1 回だけ呼ばれる)
- 修正前のコードでは `test/datachannel_closed.cpp` が失敗すること (DataChannel の close では通知されず、15 秒後に `PeerConnectionState::kFailed` で通知される)
- E2E テスト (`test_sumomo_data_channel_signaling` / `test_sumomo_sendonly_recvonly`) が通ること
