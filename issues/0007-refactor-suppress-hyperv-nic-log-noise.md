# Windows E2E の Hyper-V NIC UDP send エラーログノイズを抑制する

- Priority: Low
- Created: 2026-06-11
- Completed: {YYYY-MM-DD}
- Model: Opus 4.7
- Branch: feature/refactor-suppress-hyperv-nic-log-noise
- Polished: 2026-07-10
## 目的

Windows GitHub-hosted runner (`windows-2025`) 上での E2E テストで、libwebrtc の ICE 候補収集時に Hyper-V 仮想 NIC からの UDP send が `error 10051 (WSAENETUNREACH)` で大量に失敗するログが出力されている。実害はなく、別 NIC (`Microsoft:10.1.0.x`) 経由で TURN allocate と ICE 確立は成功しているが、ログが汚れて本物の問題との切り分けが困難になる。CI workflow の Windows E2E ジョブで該当 NIC を pytest 実行前に無効化することでログを抑制する。

## 優先度根拠

実害はないため Low。フレーキー発生時の一次切り分けの際にノイズが多いと判別時間がかかるため副次的に改善したい程度。本 issue は他の issue の完了を待たず独立して着手可能。

## 現状

2026-06-11 schedule の windows_x86_64 ジョブ (run [27318676246](https://github.com/shiguredo/sora-cpp-sdk/actions/runs/27318676246)、job id 80706826054) の failed log で `error 10051` の文字列を含む行は 7 件、`10051` の文字列を含む行は 15 件確認 (`gh run view --job=80706826054 --log-failed | grep -c "error 10051"` の出力)。ログ抜粋:

```
[000:898][252] (stun_port.cc:324): Port[...:Hyper-V:172.17.80.x/32:Ethernet:id=4]: UDP send of 20 bytes to host sora-turn-...sora-labo.shiguredo.app:55840 failed with error 10051
[000:918][252] (turn_port.cc:935): Port[...:Hyper-V:172.17.80.x/32:Ethernet:id=4]: Failed to send TURN message, error: 10051
[000:918][252] (turn_port.cc:611): Port[...:Hyper-V:172.17.80.x/32:Ethernet:id=4]: Connection with server failed with error: 10051
```

一方、`Microsoft:10.1.0.x` interface 経由では TURN allocate に成功し、ICE 確立も完了している。

### SDK 側で対応できない理由

事前調査により以下を確認した:

- Sora C++ SDK の公開 API (`include/sora/`) に WebRTC の NIC フィルタ (`network_ignore_list` / `SetNetworkIgnoreMask`) を露出する仕組みは無い。`SoraSignalingConfig::network_manager` は default を渡しているのみ
- sumomo (`examples/sumomo/src/sumomo.cpp`) に `--ice-network-interface` 等の NIC フィルタ引数は無い
- libwebrtc の `LogMessage::LogToDebug(LoggingSeverity)` は全域 severity 制御のみで、`stun_port.cc` / `turn_port.cc` だけをピンポイントで抑制する公式 API は無い

SDK 側で NIC フィルタ機能を新規追加するのは Priority Low の本 issue のスコープを超えるため、CI workflow 側で対処する。

## 設計方針

`.github/workflows/ci.yml` の E2E ジョブ matrix `windows_x86_64` のテスト実行ステップ前に、PowerShell で Hyper-V Default Switch の仮想 NIC (`vEthernet (Default Switch)`) を無効化するステップを追加する。

`windows-2025` GitHub-hosted runner では `Disable-NetAdapter` をジョブ実行ユーザー権限で実行可能 (Windows runner の Actions ステップは管理者権限で動作)。対象 NIC は `vEthernet (Default Switch)` 1 つに限定し、他の Hyper-V 関連ミニポート (`Hyper-V Virtual Ethernet Adapter` 系、`vEthernet (nat)` 等) は無効化しない (`Microsoft:10.1.0.x` 経由の HTTPS 接続や TURN allocate が落ちないよう範囲を最小化するため)。

無効化のスコープと安全性:
- 対象は E2E ジョブの `windows_x86_64` matrix のみ (`.github/workflows/ci.yml:350-402` の matrix 全 entry で `name:` フィールドが定義されているため `if: ${{ matrix.platform.name == 'windows_x86_64' }}` の条件式で安全に分岐できる)
- 影響範囲は同ジョブ内のテスト実行ステップに閉じる (ジョブ完了で runner ごと破棄されるため復旧不要)
- `actions/checkout` 等の前段ステップは無効化対象の `vEthernet (Default Switch)` ではなく `Microsoft:10.1.0.x` 経由で HTTPS 接続している (現状で ICE 確立も同 NIC 経由で成功している事実から確認) ためジョブ全体への副作用なし

## 完了条件

- `.github/workflows/ci.yml` の E2E ジョブ matrix `windows_x86_64` で、`uv run pytest -v -x` ステップ（現状 `.github/workflows/ci.yml:535`）より前に `vEthernet (Default Switch)` を無効化する PowerShell ステップが追加されている
- 当該ステップは `if: ${{ matrix.platform.name == 'windows_x86_64' }}` で windows_x86_64 ジョブのみに適用される
- 当該ステップが PR の同 windows_x86_64 ジョブで成功し、無効化対象 NIC が 1 つ以上 disable できたログ（例: `Disabling NIC: vEthernet (Default Switch) ...`）を残している
- 同 PR の windows_x86_64 ジョブの後段 pytest ステップで `Hyper-V:172.17.80.x/32` を含む `error 10051` 行がゼロになっていることを実機ログで確認する。Hyper-V Default Switch 以外の経路で `10051` が出る場合は本 issue のスコープ外とし、別途必要なら新規 issue で扱う
- `CHANGES.md` の `## develop` 配下、`### misc` セクションに以下の形式で `[FIX]` エントリを追記する。`### misc` セクション内では凡例順（CHANGE → ADD → UPDATE → FIX）を尊重し、先頭の `[FIX]` エントリの直後に挿入する。`### misc` 内既存エントリの並べ替えは本 issue のスコープ外。担当者ハンドル `@<担当者>` は PR 作成者のものに書き換える:

  ```
  - [FIX] Windows E2E の Hyper-V NIC UDP send エラーログノイズを抑制する
    - GitHub-hosted Windows runner の Hyper-V 仮想 NIC が原因で libwebrtc が WSAENETUNREACH (10051) を大量に吐いていたのを修正する
    - .github/workflows/ci.yml の E2E windows_x86_64 ジョブのテスト実行前に Hyper-V 仮想 NIC を無効化するステップを追加する
    - @<担当者>
  ```


## 解決方法

`.github/workflows/ci.yml` の E2E ジョブ matrix `windows_x86_64` の `uv run pytest -v -x` ステップ（現状 535 行付近）の直前に、以下の PowerShell ステップを追加する。対象 NIC を `vEthernet (Default Switch)` の 1 つに限定し、無効化前後の `Get-NetAdapter` 出力をログに残す。

```yaml
      - name: Disable Hyper-V Default Switch NIC (windows_x86_64 only)
        if: ${{ matrix.platform.name == 'windows_x86_64' }}
        shell: pwsh
        run: |
          Write-Host '--- Before ---'
          Get-NetAdapter | Format-Table Name, InterfaceDescription, Status
          $target = Get-NetAdapter -Name 'vEthernet (Default Switch)' -ErrorAction SilentlyContinue
          if (-not $target) {
            Write-Error 'NIC ''vEthernet (Default Switch)'' not found. windows-2025 runner image may have changed; investigate before proceeding.'
            exit 1
          }
          Write-Host "Disabling NIC: $($target.Name) ($($target.InterfaceDescription))"
          Disable-NetAdapter -Name $target.Name -Confirm:$false
          Write-Host '--- After ---'
          Get-NetAdapter | Format-Table Name, InterfaceDescription, Status
```

実装後、PR の Actions 上で当該ステップが緑になり、`Before` / `After` のログで無効化対象が確かに disable されたことを確認したうえで、後続の pytest ステップで `Hyper-V:172.17.80.x/32` を含む `error 10051` 行がゼロになったことを実機ログで確認する。

### スコープ外

- libwebrtc のログ出力カテゴリ別抑制 (現状 libwebrtc 公式 API では実現困難なため別途検討)
- Sora C++ SDK 側への NIC フィルタ API 露出 (Priority Low の本 issue で扱うには影響範囲が大きすぎるため)
- 他 self-hosted runner や macOS / Linux runner への同種対策 (現状ノイズが観測されているのは GitHub-hosted Windows runner のみ)
