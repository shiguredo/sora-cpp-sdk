# DataChannel が閉じられた際にクライアントが切断されない問題を修正する

- Created: 2026-09-10
- Completed: YYYY-MM-DD
- Branch: feature/fix-disconnect-on-datachannel-closed
- Polished: YYYY-MM-DD
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
   ./sumomo --signaling-url wss://<signaling-url>/signaling --role sendrecv --channel-id <channel-id> --multistream true --data-channel-signaling true
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

- DataChannel シグナリング利用中に任意の DataChannel が閉じられた場合、`OnDisconnect` が 1 回呼ばれること
- `{"type":"close"}` によるサーバ起点のグレースフルシャットダウンでは、従来どおりすべての DataChannel の close を待って通知され、二重通知や close コード・reason の喪失がないこと
- クライアント起点の `Disconnect()` で二重通知が発生しないこと
- 回帰がないこと: `test/datachannel.cpp` の DataChannel 送受信テストと、E2E テスト (`test_sumomo_data_channel_signaling`) が通ること
- DataChannel の一方的な close は Sora のデバッグ API が必要で E2E テストに組み込めないため、sumomo とデバッグ API による手動検証で `OnDisconnect` が呼ばれることを確認する
- 変更履歴 (`CHANGES.md`) の `## develop` の `[FIX]` にエントリを追記する
