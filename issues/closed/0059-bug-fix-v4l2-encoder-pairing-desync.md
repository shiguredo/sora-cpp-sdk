# V4L2 M2M エンコーダのフレームペアリングズレでサイマルキャストレイヤーが復帰不能になる

- Created: 2026-08-19
- Completed: 2026-09-18
- Branch: feature/fix-v4l2-encoder-pairing-desync
- Polished: 2026-08-19

## 目的

Raspberry Pi の V4L2 M2M H264 エンコーダで、デバイス出力とコールバックのペアリングが 1 度ズレると、以後の全フレームが `V4L2H264Encoder::SendFrame` のタイムスタンプ一致チェックで捨てられ続け、サイマルキャストのレイヤーが復帰不能になる。この状態を防ぎ、発生した場合は検知して再同期できるようにする。

2026-08-19 の schedule CI (run 32205362822) で Raspberry Pi E2E の `test_simulcast` が 3 試行すべて失敗した。r0 レイヤーの outbound-rtp 統計に `frameWidth` が出力されず `KeyError` になったため、E2E の安定性だけでなく実際の映像配信品質にも影響する問題である。

## 現状

### 発生事象

- `src/hwenc_v4l2/v4l2_runner.cpp` の `V4L2Runner::PollProcess` は、`Enqueue()` で登録されたコールバック (`on_completes_`) とデバイスの capture 出力を FIFO でペアリングする
- `V4L2Runner::Enqueue` は `VIDIOC_QBUF` の後に `on_completes_` へ登録していた。デバイスは投入された入力をすぐ処理できるため、poll スレッドが対応する capture バッファを先に取り出すと `[POLL][H264Encoder] on_completes_ is empty.` が出る。このときバッファは再エンキューされる (`cfe2a7ad` で修正済み) が、**ペアリングのズレ自体は回復しない**
- ズレた状態では `src/hwenc_v4l2/v4l2_h264_encoder.cpp` の `V4L2H264Encoder::SendFrame` が `frame.timestamp_us() != timestamp_us` を検出し、`WEBRTC_VIDEO_CODEC_ERROR` を返してフレームを捨てる
- 以降も入力フレームはエンコーダに届き続けるが、タイムスタンプが一致し続けないため、**全フレームが捨てられ続ける**。キャプチャバッファのタイムスタンプは `V4L2_BUF_FLAG_TIMESTAMP_COPY` で入力フレームの値がコピーされるため、ペアリングが 1 つズレた状態では常に 1 フレーム前の入力のタイムスタンプと比較され、単調増加するタイムスタンプ同士は再一致しない
- サイマルキャスト時はレイヤーごとに独立したエンコーダインスタンスがあるため、ズレたレイヤーだけが死ぬ。該当レイヤーは以後エンコード結果が捨てられ続けるため、outbound-rtp 統計に `frameWidth` が出力されなくなり (VideoSendStream の内部統計では `width: 0` / `height: 0`)、RTP 送出がほぼ停止する

### 観測されたログ (2026-08-19 schedule CI, run 32205362822)

- `[001:702][16061] [POLL][H264Encoder] on_completes_ is empty.` の直後から `SendFrame  Frame parameter is not found. SkipFrame` が約 20 秒間継続
- 同じ事象が 2 つのエンコーダインスタンスで発生 (1.7 秒時点と 15.3 秒時点)
- 最終統計で r0 レイヤーは `key: 2, delta: 30` のみで `width: 0, height: 0`。r1 レイヤーも `key: 2, delta: 426` のまま送出停止しており、1.7 秒時点と 15.3 秒時点の 2 つのズレで 2 レイヤーが死んでいる (送信中なのは r2 のみ)
- 同一コミット `adb2f5ac` の前日 run (32125492859) は成功しており、コード変更による回帰ではない。デバイス・タイミング依存の事象

### 再現性

決定的には再現しない。発生は poll スレッドと `Enqueue` の実行タイミングに依存する。一度発生すると同じセッション内では復帰しないため、E2E の 3 試行すべてが同じエラーで失敗する。

## 設計方針

修正の中心は「ペアリングズレの発生防止」とし、そのうえで「ズレの検知と再同期」も入れる。`V4L2Runner` はエンコーダ (`V4L2H264EncodeConverter`) だけでなくスケーラー (`V4L2ScaleConverter`)・デコーダ (`V4L2DecodeConverter`) からも共用されているため、ランナー共通の修正を基本とする。観測された 2 事象とも `on_completes_` が空 → タイムスタンプ不一致の順で始まっている。タイムスタンプの意味論はエンコーダ・スケーラーが実 us なのに対しデコーダは RTP タイムスタンプを us として扱った値のため、タイムスタンプで検証する候補を共通化する際の注意点とする。

- `V4L2Runner::Enqueue` で `VIDIOC_QBUF` より先にコールバックを登録し、レースを閉じる
- `V4L2Runner` のペアリングをタイムスタンプで検証し、ズレを検知したらキューを再同期する

E2E テスト側のフレーキー対策 (r0 の `frameWidth` チェックの緩和など) は本 issue のスコープ外とし、必要なら別 issue とする。

## 完了条件

- V4L2 M2M エンコーダで `on_completes_` が空になる事象が発生しても、以後のフレームがタイムスタンプ不一致で捨てられ続けないこと (発生を防ぎ、発生した場合も再同期により回復すること)
- 回帰がないこと: raspberry_pi の E2E テスト (`test_connection_stats`, `test_simulcast`) が通ること
- 発生が決定的に再現できないため、コードレビューでペアリングの整合性を検証する (モック・スタブは使用しない。既存 issue 0052 と同方針)
- 変更履歴 (`CHANGES.md`) の `## develop` セクションのコア SDK の `[FIX]` 群にエントリを追記する

## 解決方法

`V4L2Runner::Enqueue` が `VIDIOC_QBUF` の後に `on_completes_` へ登録していたのを、先に登録するように直した。あわせて、ペアリングが崩れた場合に検知して復帰できるようにし、投入に失敗した出力バッファが失われる経路も直した。

### 原因

`V4L2Runner::Enqueue` が `VIDIOC_QBUF` を実行した後に `on_completes_` へコールバックを登録していた。デバイスは投入された入力をすぐに処理できるため、`QBUF` が返ってから登録を積むまでの間に poll スレッドが対応する capture バッファを取り出すと、登録が無いまま 1 フレーム分の対応がずれる。

ずれると以降の全フレームが 1 つ前の入力と対応付けられ、`V4L2H264Encoder::SendFrame` の `frame.timestamp_us() != timestamp_us` が常に成立して全フレームが捨てられる。タイムスタンプは単調増加するため再一致しない。

bcm2835-codec の `op_buffer_cb` を確認した限り、デバイスは投入された `INPUT` を消費したときにだけ `CAPTURE` を 1 つ出力し、SDK 側のこのレース以外にペアリングが崩れる経路は見つからなかった。このため、原因はデバイスではなく `Enqueue` の登録順である。

### 変更内容

`src/hwenc_v4l2/v4l2_runner.cpp`:

- `V4L2Runner::Enqueue` で `VIDIOC_QBUF` より先に `on_completes_` へ登録する。失敗した場合は登録を取り消す
- `V4L2Runner::PollProcess` で capture バッファのタイムスタンプと登録のタイムスタンプを照合し、一致しない登録を破棄してペアリングを組み直す。`Enqueue` が先に登録するため通常は先頭で一致し、破棄ループは 0 回で抜ける
- 破棄した登録の出力バッファはデバイスが入力を消費した時点で `DQBUF` で返却されるため、破棄時には何もしない (返却すると同じインデックスが二重に利用可能になる)
- `VIDIOC_QBUF` に失敗した場合は、そのバッファのインデックスを `ReturnAvailableOutputBuffer` で再利用可能に戻す。従来はインデックスが失われ、4 回失敗すると `No available output buffers` が恒久化していた
- 出力バッファを利用可能にする経路を `ReturnAvailableOutputBuffer` に統一する
- `on_next` は上位のバッファに保持されて `V4L2Runner` より長生きしうるため、`EnqueueCaptureBuffer` を `fd` を受け取る static 関数にし、`on_next` は `fd` を値で捕捉する

`src/hwenc_v4l2/v4l2_converter.cpp`:

- エンコーダ・スケーラー・デコーダの 3 か所の `Enqueue` 呼び出しで戻り値を確認し、失敗時はエラーを返すようにする
- タイムスタンプの引数を削除する (`Enqueue` が `v4l2_buf` から計算する)

`include/sora/hwenc_v4l2/v4l2_runner.h`:

- `Enqueue` から `timestamp_us` の引数を削除し、`Registration` は `on_completes_` の要素としてタイムスタンプと `on_complete` を持つ
- タイムスタンプの変換は `BufferTimestampUs` にまとめ、設定側と計算側で同じ変換を使う

`CHANGES.md`:

- `## develop` の `[FIX]` に追記する

### 検証結果

- `clang-format` (`clang-format-21`) で変更した 3 ファイルに差分が出ないことを確認した
- libwebrtc m154 のヘッダと Raspberry Pi 向けの m141 のヘッダの両方で、`v4l2_runner.cpp` と `v4l2_converter.cpp` を `-fsyntax-only` で型検査し、エラーが出ないことを確認した
- レビューでバッファの会計 (正常時・再同期時・投入失敗時・シャットダウン時) と再同期ループの終了性を確認した。ズレが起きない限り破棄ループは実行されず、実行された場合も登録が尽きるまでで必ず終了する

### 未検証

raspberry_pi の E2E テスト (`test_connection_stats`, `test_simulcast`) は実機と Sora サーバーが必要なため、この環境では実行していない。
