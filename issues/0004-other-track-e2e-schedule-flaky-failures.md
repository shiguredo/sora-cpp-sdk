# E2E スケジュール CI のフレーキー失敗を追跡する

- Priority: High
- Created: 2026-06-11
- Polished: {YYYY-MM-DD}
- Model: Opus 4.7
- Branch: feature/refactor-track-e2e-schedule-flaky-failures

## 目的

develop ブランチの schedule CI が 2026-06-09 以降 3 日連続で E2E テストの間欠的失敗により赤になっている。SDK 側コードは無変更にもかかわらず再現性なく失敗するため、原因は SDK 内部ではなく Sora Labo 側または各ランナーのネットワーク経路に起因する可能性が高い。本 issue は現象を記録し、SDK 側で取りうる「フレーキー耐性を上げる」「ログを切り分けやすくする」対策を子 issue にブレイクダウンして追跡する。

## 優先度根拠

3 日連続で schedule CI が赤になっており、本来は SDK 側の退行を検知する役割が失われている。本物の退行が混入していても気付けないリスクが高いため High。

## 現状

### 失敗ジョブ一覧 (gh で取得した実ログより)

| 日付 | ジョブ | 失敗テスト | 直接原因 |
|---|---|---|---|
| 2026-06-09 | E2E ubuntu-22.04_x86_64 | test_sumomo_sendonly_recvonly[VP9] | httpx.ConnectError + 直前に wscode=4490 wsreason=TIMEOUT |
| 2026-06-10 | E2E ubuntu-22.04_x86_64 | test_sumomo_sendrecv_pair[VP9] | httpx.ConnectError |
| 2026-06-10 | E2E ubuntu-24.04_x86_64 | test_sumomo_sendonly_recvonly[AV1] | フレーム未受信 (assert 0 > 0) |
| 2026-06-10 | E2E nvidia_video_codec | test_sendonly_recvonly[H264] | Process exited + wscode=4490 wsreason=TIMEOUT |
| 2026-06-11 | E2E windows_x86_64 | test_sumomo_sendrecv_pair[VP8] | sumomo.exe has crashed (exit code: 0) + wscode=4490 wsreason=INTERNAL-ERROR |
| 2026-06-11 | E2E ubuntu-22.04_armv8 | test_sumomo_resolutions[VGA-1000-640-480] | assert None is not None (WS 5 秒で切断) |

### 共通点

- 接続先は全て Sora Labo (`sora.sora-labo.shiguredo.app`)
- Sora 側からの能動切断 (`wscode=4490 wsreason=TIMEOUT` / `INTERNAL-ERROR`) が複数のケースで観測されている
- 失敗するジョブ・テスト・コーデックは日ごとにバラバラで再現性が低い
- 2026-06-08 schedule までは安定動作。develop の SDK 側コード変更を挟まずに 2026-06-09 から発生
- ビルドは全プラットフォーム成功し、E2E のみ落ちる

### Windows 特有のノイズ

windows_x86_64 ジョブでは Hyper-V NIC (`172.17.80.x/32`) からの UDP send が `error 10051 (WSAENETUNREACH)` で大量に失敗するログが出ているが、別 NIC (`Microsoft:10.1.0.x`) 経由で ICE 確立は成功しており、フレーキーの直接原因ではない。ただし切り分けの妨げになるため別 issue で扱う。

## 設計方針

SDK 側コードに退行はないと推定されるため、根本原因 (Sora Labo) は別途確認するとして、SDK 側では以下 3 点で「フレーキーに強くする」「ログを切り分けやすくする」対策を進める。それぞれ子 issue として独立して追跡する。

- 0005: フレーキーなテストに `pytest-retry` を適用する
- 0006: e2e-test の `sumomo.py:809-812` の Windows 用 crashed 判定を改善する (exit code 0 を crashed と表現しない)
- 0007: Windows E2E の Hyper-V NIC UDP send エラー (WSAENETUNREACH 10051) のログノイズを抑制する

本 issue 自体はトラッキング目的であり、実装は子 issue で行う。本 issue のブランチでは `CHANGES.md` の `## develop` セクションに `[FIX]` 系のエントリで現象を記録するメモを追記する程度の作業のみを想定する。

## 完了条件

- 0005, 0006, 0007 の対応が全て完了している
- 上記対応後、schedule CI が連続 7 日以上 (= 1 週間以上) 緑のまま維持できることを確認できている
- Sora Labo 側の `wscode=4490 wsreason=TIMEOUT` / `INTERNAL-ERROR` の発生原因は本 issue のスコープ外であり、SDK 側ではコントロールできない旨を `CHANGES.md` などに明記する

## 解決方法

子 issue (0005, 0006, 0007) の対応を順次完了させる。子 issue 全完了後、schedule CI を 1 週間観測して安定を確認したうえで本 issue をクローズする。
