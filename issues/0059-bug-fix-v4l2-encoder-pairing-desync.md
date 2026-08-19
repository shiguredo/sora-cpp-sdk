# V4L2 M2M エンコーダのフレームペアリングズレでシミュラキャストレイヤーが復帰不能になる

- Created: 2026-08-19
- Completed: YYYY-MM-DD
- Branch: feature/fix-v4l2-encoder-pairing-desync
- Polished: YYYY-MM-DD

## 目的

Raspberry Pi の V4L2 M2M H264 エンコーダで、デバイス出力とコールバックのペアリングが 1 度ズレると、以後の全フレームが `V4L2H264Encoder::SendFrame` のタイムスタンプ一致チェックで捨てられ続け、シミュラキャストのレイヤーが復帰不能になる。この状態を検知して再同期できるようにする。

2026-08-19 の schedule CI (run 32205362822) で Raspberry Pi E2E の `test_simulcast` が retry 3 回すべて失敗した。r0 レイヤーの outbound-rtp 統計に `frameWidth` が出力されず `KeyError` になったため、E2E の安定性だけでなく実際の映像配信品質にも影響する問題である。

## 現状

### 発生事象

- `src/hwenc_v4l2/v4l2_runner.cpp` の `V4L2Runner::PollProcess` は、`Enqueue()` で登録されたコールバック (`on_completes_`) とデバイスの capture 出力を FIFO でペアリングする
- デバイスがコールバック未登録のまま余分な capture バッファを出力すると `[POLL][H264Encoder] on_completes_ is empty.` がログに出る。このときバッファは再エンキューされる (`cfe2a7ad` で修正済み) が、**ペアリングのズレ自体は回復しない**
- ズレた状態では `src/hwenc_v4l2/v4l2_h264_encoder.cpp` の `V4L2H264Encoder::SendFrame` が `frame.timestamp_us() != timestamp_us` を検出し、`WEBRTC_VIDEO_CODEC_ERROR` を返してフレームを捨てる
- 以降も入力フレームはエンコーダに届き続けるが、タイムスタンプが一致し続けないため、**全フレームが捨てられ続ける**
- シミュラキャスト時はレイヤーごとに独立したエンコーダインスタンスがあるため、ズレたレイヤーだけが死ぬ。該当レイヤーの outbound-rtp 統計が `width: 0` / `height: 0` のままとなり、RTP 送出がほぼ停止する

### 観測されたログ (2026-08-19 schedule CI, run 32205362822)

- `[001:702][16061] [POLL][H264Encoder] on_completes_ is empty.` の直後から `SendFrame  Frame parameter is not found. SkipFrame` が約 20 秒間継続
- 同じ事象が 2 つのエンコーダインスタンスで発生 (1.7 秒時点と 15.3 秒時点)
- 最終統計で r0 レイヤーは `key: 2, delta: 30` のみで `width: 0, height: 0`
- 同一コミット `adb2f5ac` の前日 run (32125492859) は成功しており、コード変更による回帰ではない。デバイス・タイミング依存の事象

### 再現性

決定的には再現しない。発生は V4L2 M2M デバイス (bcm2835 / rp1 コーデック) の出力タイミングに依存する。一度発生すると同じセッション内では復帰しないため、E2E の retry 3 回すべてが同じエラーで失敗する。

## 設計方針

修正の中心は「ペアリングズレの検知と再同期」とする。以下の候補を実装時に評価する:

- `V4L2Runner` のペアリングをタイムスタンプで検証し、ズレを検知したらキューを再同期する
- `V4L2H264Encoder::SendFrame` のタイムスタンプ不一致時に、フレームを捨てるだけでなくペアリングをリセットする動作にする
- デバイスが余分な capture バッファを出力した時点 (`on_completes_` が空) で、その後のペアリングを検証・復旧する

E2E テスト側のフレーキー対策 (r0 の `frameWidth` チェックの緩和など) は本 issue のスコープ外とし、必要なら別 issue とする。

## 完了条件

- V4L2 M2M エンコーダで `on_completes_` が空になる事象が発生しても、以後のフレームがタイムスタンプ不一致で捨てられ続けないこと (再同期により回復すること)
- 回帰がないこと: raspberry_pi の E2E テスト (`test_sendonly`, `test_simulcast`) が通ること
- 発生が決定的に再現できないため、コードレビューでペアリングの整合性を検証する (モック・スタブは使用しない。既存 issue 0052 と同方針)
- 変更履歴 (`CHANGES.md`) の `## develop` セクションのコア SDK の `[FIX]` 群にエントリを追記する
