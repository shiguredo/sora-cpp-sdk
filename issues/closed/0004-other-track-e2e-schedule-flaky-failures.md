# E2E スケジュール CI のフレーキー失敗を追跡する

- Priority: High
- Created: 2026-06-11
- Polished: 2026-06-11
- Model: Opus 4.7
- Branch: feature/refactor-e2e-schedule-flaky-failures-tracking

## 目的

develop ブランチの schedule CI が 2026-06-09 以降 3 営業日連続で E2E テストの間欠的失敗により赤になっている。失敗ジョブ・テスト・コーデック・症状が日ごとに分散しており、SDK 内部の単一バグでは説明しにくい。本 issue は失敗事例の一次資料を 1 か所に集約し、SDK 側で取れる対策を子 issue にブレイクダウンして追跡する。現状把握では症状が複数系統に分かれており、症状ごとの真因が SDK 側にあるか Sora Labo 側にあるかを SDK 側からは特定できないため、まず `pytest.mark.flaky` で抑え込みつつ schedule を緑に戻したうえで、フレーキー再発の傾向を観測する方針を取る。

## 優先度根拠

3 営業日連続で schedule CI が赤のままであり、本来 SDK 側の退行を検知すべき仕組みが本物の退行を取り逃すリスクが高い状態のため High。

## 現状

### 失敗ジョブ一覧

`gh run view --job=<job-id> --log-failed` で取得した一次ログから抜粋。

| 日付 | run ID | ジョブ | 失敗テスト | 観測症状 |
|---|---|---|---|---|
| 2026-06-09 | [27178514150](https://github.com/shiguredo/sora-cpp-sdk/actions/runs/27178514150) | E2E ubuntu-22.04_x86_64 | `test_sumomo_basic.py::test_sumomo_sendonly_recvonly[VP9]` | 直前に Sora 側 WS 切断 (`wscode=4490 wsreason=TIMEOUT`) → sumomo HTTP に到達せず `httpx.ConnectError` |
| 2026-06-10 | [27247692489](https://github.com/shiguredo/sora-cpp-sdk/actions/runs/27247692489) | E2E ubuntu-22.04_x86_64 | `test_sumomo_basic.py::test_sumomo_sendrecv_pair[VP9]` | `httpx.ConnectError` (sumomo HTTP に到達せず、`wscode` ログなし) |
| 2026-06-10 | [27247692489](https://github.com/shiguredo/sora-cpp-sdk/actions/runs/27247692489) | E2E ubuntu-24.04_x86_64 | `test_sumomo_basic.py::test_sumomo_sendonly_recvonly[AV1]` | フレーム未受信 (`assert 0 > 0`、`wscode` ログなし) |
| 2026-06-10 | [27247692489](https://github.com/shiguredo/sora-cpp-sdk/actions/runs/27247692489) | E2E nvidia_video_codec | `test_sumomo_nvidia_video_codec.py::test_sendonly_recvonly[H264]` | `Process exited unexpectedly with code 0` + `wscode=4490 wsreason=TIMEOUT` |
| 2026-06-11 | [27318676246](https://github.com/shiguredo/sora-cpp-sdk/actions/runs/27318676246) | E2E windows_x86_64 | `test_sumomo_basic.py::test_sumomo_sendrecv_pair[VP8]` | `sumomo.exe has crashed (exit code: 0)` + `wscode=4490 wsreason=INTERNAL-ERROR` |
| 2026-06-11 | [27318676246](https://github.com/shiguredo/sora-cpp-sdk/actions/runs/27318676246) | E2E ubuntu-22.04_armv8 | `test_sumomo_basic.py::test_sumomo_resolutions[VGA-1000-640-480]` | `assert None is not None` (WS が `type=connect` 送信後約 5 秒で graceful close、`wscode` ログなし) |

### 症状の系統

6 ケースは以下の独立した可能性のある 3 系統に分かれる。pytest-retry で吸収できる前提は系統 A / B / D で成立しうるが、系統 C は SDK 側退行の可能性が残る点に注意する。

- **系統 A**: Sora 側からの WS close 受信 (`wscode=4490`)。`src/sora_signaling.cpp` の `wscode=` 出力は Boost.Beast `websocket::stream::reason()` 由来で、4490 は SDK 内では生成しないため Sora 側送信の close と確定できる (RFC 6455 の private-use 枠 4000-4999)。3 ケース該当 (06-09 ubuntu-22.04_x86_64、06-10 nvidia_video_codec、06-11 windows_x86_64)
- **系統 B**: WS の graceful close (`wscode` ログなし) または `httpx.ConnectError` のみ。Sora 側 close か runner 側ネットワーク不通かは本ログだけでは確定できない。2 ケース該当 (06-10 ubuntu-22.04_x86_64、06-11 ubuntu-22.04_armv8)
- **系統 C**: フレーム未受信 (`assert 0 > 0`)。WS 切断なし。SDK の RTP 受信処理バグまたは Sora Labo 側の送出停止のいずれか。1 ケース該当 (06-10 ubuntu-24.04_x86_64)

### 共通点

- 接続先は全て Sora Labo (`sora.sora-labo.shiguredo.app`)
- 失敗テスト関数とコーデックの組み合わせは日ごとに分散しており、単一テストの常時失敗ではない
- schedule の cron は `.github/workflows/ci.yml:13-16` で月-金実行。2026-06-08 (月) schedule (run [27111697657](https://github.com/shiguredo/sora-cpp-sdk/actions/runs/27111697657)) は成功し、2026-06-09 (火) から赤
- 上記 3 営業日の各 run でビルドジョブはいずれも成功している

### 06-05 の libwebrtc m150 アップグレードとの関係

2026-06-05 に PR #326 (`feature/update-webrtc-m150`) がマージされている (merge commit `79d071c0`、version bump は commit `41404683`)。マージ直後の 2026-06-08 (月) schedule は成功しているため m150 が決定的な回帰要因とは考えにくいが、子 issue で吸収しきれなかった場合の追加検証候補として保留する。

## 表追記の運用ルール

新たに schedule run の失敗を本 issue 表に追記する基準:

- 失敗ログに `wscode=4490` を含む (系統 A 確定)、または
- 失敗テストの `pytest` nodeid (`test_<file>.py::<function>` 部分。パラメータ ID は問わない) が上表のいずれかと一致する

上記いずれも満たさない失敗は本表に追記せず、退行の可能性として個別対応する。系統 C (フレーム未受信) の再発が続く場合は pytest-retry で隠蔽されるリスクがあるため、`wscode` を伴わない `assert 0 > 0` 系の失敗を観測したら 0005 適用後でも個別に退行調査する。

## 設計方針

SDK 側で取れる対策を以下の子 issue に分割する。

| 子 issue | 内容 | Priority | CI 緑化への寄与 |
|---|---|---|---|
| [0005](0005-test-apply-pytest-retry-to-flaky-tests.md) | raspberry_pi で稼働中の `pytest.mark.flaky` を他テストファイルにも適用 | High | 直接 |
| [0006](0006-refactor-sumomo-py-windows-exit-detection.md) | `sumomo.exe has crashed` のメッセージを exit code 0 と非 0 で分離 | Low | なし |
| [0007](0007-refactor-suppress-hyperv-nic-log-noise.md) | Windows runner の Hyper-V NIC UDP send エラーログを抑制 | Low | なし |

### 着手順序

本 issue は追跡用 (メタ) issue で実コード変更を持たないため、shiguredo-issues 規約「番号が小さい issues から順番に対応する」の例外として、実コードを伴う子 (0005/0006/0007) を先に処理する。

1. 0005 を最優先で実装してマージする (CI 緑化に直接寄与する唯一の子 issue)
2. 0005 マージ後、0006 / 0007 はそれぞれの Priority に従って個別に進める。本 issue のクローズ判定は 0005 のみに依存する
3. 0005 マージ後も schedule が緑化しない場合、優先順に (1) Sora Labo 側ログ照合、(2) runner 側ネットワーク経路調査、(3) m150 リバート検証 を別 issue として起票する。優先順は実施コストが低い順 (Sora Labo 側ログは時雨堂内で参照可能、runner 環境再現は困難、m150 リバートはコード変更を伴う)

## 完了条件

- **0005** が完了している (0006 / 0007 は Priority Low かつ CI 緑化に寄与しないため本 issue のクローズ条件に含めない)
- 以下のいずれかが成立している:
  - 0005 マージ後の最初の schedule から **連続 5 回 (= 1 営業週) 緑** を確認できている
  - 0005 マージの UTC 日付から **暦上 28 日 (= 4 週間。連続 5 回緑判定の最大 4 サイクル分。これでも揃わなければ収束見込みなしと判断)** を経過し、それでも連続 5 回緑が揃わない場合は「SDK 側で取れる対策は完了」と判断し、着手順序ステップ 3 の別 issue へ観測責任を移譲する
- 緑判定の確認コマンドは以下 (`YYYY-MM-DD` は 0005 マージの UTC 日付の翌 UTC 日付):

  ```sh
  gh run list --workflow=ci.yml --event=schedule --branch=develop \
    --status=completed --created='>=YYYY-MM-DD' --limit=10 \
    --json conclusion,createdAt \
    --jq '{count: length, conclusions: [.[] | .conclusion]}'
  ```

  出力の `conclusions` 配列で先頭から 5 件以上連続して `"success"` が並べばクローズ判定可。途中に `"failure"` / `"cancelled"` が混じった場合は起点を最新の失敗の UTC 日付の翌 UTC 日付に更新して再カウントする。ただし暦上 28 日の上限は据え置く

## 解決方法

完了条件で示した `gh run list` の出力で連続 5 回緑を確認できた、または暦上 28 日を経過した時点で、確認した run ID または移譲先 issue 番号を本セクションに追記し、`Completed:` を更新したうえで `issues/closed/` に `git mv` する。

クローズ後に schedule が再び赤化した場合は本 issue を reopen せず、新規 issue を `issues/` 直下に起こして本 issue を関連 issue として参照する。
