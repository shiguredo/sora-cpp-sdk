# 0011 再生デバイス個別指定時の挙動を pytest で検証する

- Priority: Medium
- Created: 2026-06-23
- Completed: 2026-06-23
- Model: Kimi Code CLI
- Branch: feature/add-audio-playout-device-tests
- Polished: 2026-06-23
- Reporter: @voluntas

## 目的

#331 で修正済みの issue 0009 の完了条件「デバイス名未指定、存在しないデバイス名指定時の既存挙動が退化しないこと」のうち、再生デバイス側を pytest で検証する。

## 優先度根拠

issue 0009 の完了条件に「デバイス名未指定、存在しないデバイス名指定時の既存挙動が退化しないこと」が明記されているが、#331 では再生デバイス側の個別指定時の挙動を自動テストで検証していなかった。回帰防止のため、self-hosted runner で実行される実機デバイス向けテストを追加する。

## 現状

- `e2e-test/test_sumomo_device.py` には `--audio-recording-device` 個別指定テストがあるが、`--audio-playout-device` 個別指定テストはない。
- 再生デバイスを検証するには、同じチャネルに音声を送信する sendonly 相手プロセスが必要である。`test_sumomo_device.py` の既存テストは単一の sumomo プロセスで自己完結している。
- `examples/sumomo/src/sumomo.cpp` のデフォルトログレベルは `LS_ERROR` である。`src/sora_client_context.cpp` の `Succeeded SetPlayoutDevice` ログは `LS_INFO` で出力されるため、テスト側で `--log-level info` を指定する必要がある。
- `sumomo.py` の `Sumomo` クラスは `subprocess.Popen(..., stderr=None)` でプロセスを起動しており、`stderr=subprocess.PIPE` に変更すると Windows でテストが通らなくなる制約がある。
- `CLAUDE.md` では pytest 実行時に `-v -s` オプションを付けることとしているが、`-s`（`--capture=no`）は `capfd` のキャプチャを無効化する。`capfd` を使用するテストでは `-s` を付けず、`--capture=fd` を使用する必要がある。

## 設計方針

実機デバイスが必要なテストのため、`e2e-test/test_sumomo_device.py` に追加する。

- 各テストでは `fake_capture_device=False` と `log_level="info"` を明示し、実機 ADM 経由でデバイス設定を行い、標準エラー出力から `Succeeded SetPlayoutDevice` のログを検証する。
- 再生デバイス関連のテストは recvonly モードで実行し、同じチャネルに sendonly の相手プロセスを用意して音声フレームを受信すること、および標準エラー出力から実際に選択された再生デバイス名を検証する。
- `pytest_generate_tests` に `audio_playout_device` 用のパラメータ生成を追加し、各再生デバイスを個別にテストする。
- 音声出力デバイスが 0 個の環境ではテストケースが収集されないため、実装時に空リストの場合の扱いを検討する。
- `capfd` を使用するテストでは、`-s` オプションを付けずに `--capture=fd` を使用する。`CLAUDE.md` の E2E テスト実行方針との整合性は別途検討する。

## 完了条件

- `test_sumomo_device.py` に各再生デバイスを個別に指定した recvonly 接続テスト `test_audio_playout_device` を追加する
- `test_audio_playout_device` では sendonly の相手プロセスを同じチャネルに接続し、recvonly 側で音声フレームを受信することを確認する
- 追加するテストで `fake_capture_device=False` と `log_level="info"` を指定する
- 追加するテストで標準エラー出力から `Succeeded SetPlayoutDevice:.* name=(.+?) guid=` の正規表現で実際に選択されたデバイス名を抽出し、指定したデバイス名と一致することを検証する
- `pytest_generate_tests` に `audio_playout_device` 用のパラメータ生成を追加する
- 音声出力デバイスが 0 個の場合は `pytest.skip` する、または `assert len(device_lists.audio_playout) > 0` してテストを収集しないようにする
- `CHANGES.md` の `## develop` に `[ADD]` エントリを追加する
- `.github/workflows/ci.yml` の変更は不要（`test_sumomo_device.py` 全体が device 用 self-hosted runner の matrix エントリで既に実行される）
- issue 0009 の完了条件「デバイス名未指定、存在しないデバイス名指定時の既存挙動が退化しないこと」の再生デバイス側を自動テストでカバーすること

## 解決方法

1. `pytest_generate_tests` を拡張し、`audio_playout_device` フィクスチャに対して `device_lists.audio_playout` をパラメータ化する。音声出力デバイスが 0 個の場合は `pytest.skip` するか、空リストを渡してテストを収集しないようにする。
2. `test_sumomo_device.py` に `test_audio_playout_device` を追加する。`pytest_generate_tests` で生成した `audio_playout_device` パラメータを受け取り、`--audio-playout-device` に各再生デバイスを個別に指定する。`fake_capture_device=False`、`video=False`、`audio=True`、`log_level="info"`、role="recvonly" で接続する。同じチャネルに sendonly の相手プロセスを `fake_capture_device=False`、`video=False`、`audio=True` で接続し、recvonly 側で音声フレームを受信することを確認する。さらに標準エラー出力から `Succeeded SetPlayoutDevice:.* name=(.+?) guid=` の正規表現で実際に選択されたデバイス名を抽出し、指定したデバイス名と一致することを検証する。
3. `CHANGES.md` の `## develop` に、追加するテスト内容を記載した `[ADD]` エントリを追加する。既存の `[ADD]` エントリ「sumomo の E2E テストで `--audio-recording-device` を複数の音声録音デバイスに対して個別に検証するテストを追加する」の直後に配置する。
