# Windows E2E の Hyper-V NIC UDP send エラーログノイズを抑制する

- Priority: Low
- Created: 2026-06-11
- Polished: {YYYY-MM-DD}
- Model: Opus 4.7
- Branch: feature/refactor-suppress-hyperv-nic-log-noise

## 目的

Windows GitHub-hosted runner 上での E2E テストで、libwebrtc の ICE 候補収集時に Hyper-V NIC (`172.17.80.x/32`) からの UDP send が `error 10051 (WSAENETUNREACH)` で大量に失敗するログが出力されている。実害はなく、別 NIC (`Microsoft:10.1.0.x`) 経由で TURN allocate と ICE 確立は成功しているが、ログが汚れて本物の問題との切り分けが困難になる。SDK 側または E2E テスト側で抑制できないか調査して対処する。

## 優先度根拠

実害はないため Low。ただし 0004 のフレーキー切り分けの際にノイズが多いと一次切り分けの時間がかかるため、副次的に改善したい。

## 現状

2026-06-11 schedule の windows_x86_64 ジョブで以下のログが大量に出ている (抜粋):

```
[000:898][252] (stun_port.cc:324): Port[...:Hyper-V:172.17.80.x/32:Ethernet:id=4]: UDP send of 20 bytes to host sora-turn-...sora-labo.shiguredo.app:55840 failed with error 10051
[000:918][252] (turn_port.cc:935): Port[...:Hyper-V:172.17.80.x/32:Ethernet:id=4]: Failed to send TURN message, error: 10051
[000:918][252] (turn_port.cc:611): Port[...:Hyper-V:172.17.80.x/32:Ethernet:id=4]: Connection with server failed with error: 10051
```

一方、`Microsoft:10.1.0.x` interface 経由では TURN allocate に成功し、ICE 確立も完了している。

## 設計方針

調査 → 方針決定 → 実装の順で進める。設計判断が必要なため、以下の調査を先行する。

### 調査項目

1. Sora C++ SDK 側で WebRTC の network interface フィルタリング相当の設定 (`network_ignore_mask` / `network_preference` 等) を露出しているか確認する
2. sumomo の引数として ICE 候補収集対象 NIC を指定できる仕組み (例: 既存の `--ice-network-interface` 等) があるか確認する
3. libwebrtc の `PeerConnectionFactory` / `PeerConnectionInterface::RTCConfiguration` レベルで設定可能なオプションを確認する
4. 上記が利用できない場合の代替案:
   - GitHub Actions の Windows workflow ステップで Hyper-V NIC をテスト時のみ無効化する (e2e-test 側 / CI workflow 側)
   - libwebrtc のログ出力 (`stun_port.cc`, `turn_port.cc`, `basic_port_allocator.cc`) のレベル制御で当該ログを抑制する

### 方針決定後の対応

調査結果により対応箇所が変わる:

- SDK 内部で解決可能 → SDK の引数 / 設定で NIC をフィルタリングする実装を入れる (リファクタ範囲内)
- 不可能 → 別 issue を作成し、対応箇所を E2E テスト側または CI workflow 側に移管する

## 完了条件

- 上記調査が完了し、調査結果が本 issue 本文または `CHANGES.md` のメモに記録されている
- 採用方針に従い実装が完了している、または「対応不可」「別 issue へ移管」と記録した上で本 issue がクローズされている
- 上記対応の結果として Windows E2E のログから WSAENETUNREACH (10051) のスパムが消える、または大幅に削減されていることが実機で確認できている
- `CHANGES.md` の `## develop` に対応エントリを追記する

## 解決方法

調査完了後に確定する。確定までは本 issue で進める。調査の結果対応箇所が SDK 外 (CI workflow 等) と判明した場合は、本 issue をクローズして別 issue を新規作成する。
