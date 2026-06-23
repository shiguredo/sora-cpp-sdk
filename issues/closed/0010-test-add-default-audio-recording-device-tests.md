# 0010 録音デバイス未指定・無効指定時の挙動を pytest で検証する

- Priority: Medium
- Created: 2026-06-23
- Completed: 2026-06-23
- Model: Kimi Code CLI
- Branch: feature/add-default-audio-recording-device-tests
- Polished: 2026-06-23
- Reporter: @voluntas

## 目的

#331 で修正済みの issue 0009 の完了条件「デバイス名未指定、存在しないデバイス名指定時の既存挙動が退化しないこと」のうち、録音デバイス側を pytest で検証する。

## 優先度根拠

issue 0009 の完了条件に「デバイス名未指定、存在しないデバイス名指定時の既存挙動が退化しないこと」が明記されているが、#331 では録音デバイス側の未指定・無効指定時の挙動を自動テストで検証していなかった。回帰防止のため、self-hosted runner で実行される実機デバイス向けテストを追加する。

## 現状

- `e2e-test/test_sumomo_device.py` には `--audio-recording-device` 個別指定テストがあるが、以下のテストはない。
  - 録音デバイス名未指定時の接続テスト
  - 存在しない録音デバイス名指定時のフォールバックテスト
- `test_sumomo_basic.py` は `fake_capture_device=True`（ダミー ADM）のため、0009 の実機 ADM を介したデバイス設定系挙動は検証できない。
- `examples/sumomo/src/sumomo.cpp` のデフォルトログレベルは `LS_ERROR` である。`src/sora_client_context.cpp` の `Succeeded SetRecordingDevice` ログは `LS_INFO` で出力されるため、テスト側で `--log-level info` を指定する必要がある。
- 既存の `test_audio_recording_device` も `log_level` を指定していないため、標準エラー出力からの `Succeeded SetRecordingDevice` ログ検証が機能していない可能性がある。
- `sumomo.py` の `Sumomo` クラスは `subprocess.Popen(..., stderr=None)` でプロセスを起動しており、`stderr=subprocess.PIPE` に変更すると Windows でテストが通らなくなる制約がある。
- `CLAUDE.md` では pytest 実行時に `-v -s` オプションを付けることとしているが、`-s`（`--capture=no`）は `capfd` のキャプチャを無効化する。`capfd` を使用するテストでは `-s` を付けず、`--capture=fd` を使用する必要がある。

## 設計方針

実機デバイスが必要なテストのため、`e2e-test/test_sumomo_device.py` に追加する。

- 各テストでは `fake_capture_device=False` と `log_level="info"` を明示し、実機 ADM 経由でデバイス設定を行い、標準エラー出力から `Succeeded SetRecordingDevice` のログを検証する。
- 録音デバイス関連のテストは sendonly モードで実行し、音声フレームが送信されること、および標準エラー出力から実際に選択されたデバイス名を検証する。
- 存在しない録音デバイス名には空でない文字列を使用する。空文字列は `SoraClientContextConfig::audio_recording_device` に設定されても sumomo 側で空判定により無視されるため、本テストの「無効名」対象外とする。
- 無効な録音デバイス名指定時は、デバイス一覧が空でない場合に index 0（デフォルトデバイス）にフォールバックする。デバイス一覧が空の場合は設定をスキップする。
- `capfd` を使用するテストでは、`-s` オプションを付けずに `--capture=fd` を使用する。`CLAUDE.md` の E2E テスト実行方針との整合性は別途検討する。

## 完了条件

- `test_sumomo_device.py` に録音デバイス未指定時の sendonly 接続テスト `test_default_audio_recording_device` を追加する
- `test_sumomo_device.py` に存在しない録音デバイス名指定時のフォールバックテスト `test_invalid_audio_recording_device` を追加する
- 追加するテストで `fake_capture_device=False` と `log_level="info"` を指定する
- 既存の `test_audio_recording_device` も `log_level="info"` を指定するよう修正する
- 追加するテストで標準エラー出力から `Succeeded SetRecordingDevice:.* name=(.+?) guid=` の正規表現で実際に選択されたデバイス名を抽出し、期待値と一致することを検証する
- 録音デバイスが 0 個の場合は `pytest.skip` する、または `assert len(device_lists.audio_recording) > 0` してテストを収集しないようにする
- `CHANGES.md` の `## develop` に `[ADD]` エントリを追加する
- `.github/workflows/ci.yml` の変更は不要（`test_sumomo_device.py` 全体が device 用 self-hosted runner の matrix エントリで既に実行される）
- issue 0009 の完了条件「デバイス名未指定、存在しないデバイス名指定時の既存挙動が退化しないこと」の録音デバイス側を自動テストでカバーすること

## 解決方法

1. `test_sumomo_device.py` に `test_default_audio_recording_device` を追加する。`--audio-recording-device` を指定せず、`fake_capture_device=False`、`video=False`、`audio=True`、`log_level="info"` で sendonly 接続し、音声フレームが送信されることを確認する。録音デバイスが 0 個の場合は `pytest.skip` する。さらに標準エラー出力から `Succeeded SetRecordingDevice:.* name=(.+?) guid=` の正規表現で実際に選択されたデバイス名を抽出し、デフォルトデバイス名（`device_lists.audio_recording[0]`）と一致することを検証する。
2. `test_sumomo_device.py` に `test_invalid_audio_recording_device` を追加する。`--audio-recording-device` に実在するデバイス名と衝突しない空でない無効名（例: `__nonexistent_device_for_test__`、またはタイムスタンプ・UUID を含む動的な文字列）を指定し、`fake_capture_device=False`、`video=False`、`audio=True`、`log_level="info"` で sendonly 接続する。録音デバイスが 0 個の場合は `pytest.skip` する。デフォルトデバイスにフォールバックして音声フレームが送信されることを確認する。さらに標準エラー出力から `Succeeded SetRecordingDevice:.* name=(.+?) guid=` の正規表現で実際に選択されたデバイス名を抽出し、フォールバック先のデフォルトデバイス名（`device_lists.audio_recording[0]`）と一致することを検証する。
3. 既存の `test_audio_recording_device` の `Sumomo` 呼び出しに `log_level="info"` を追加する。
4. `CHANGES.md` の `## develop` に、追加するテスト内容を記載した `[ADD]` エントリを追加する。既存の `[ADD]` エントリ「sumomo の E2E テストで `--audio-recording-device` を複数の音声録音デバイスに対して個別に検証するテストを追加する」の直後に配置する。
