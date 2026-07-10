# sumomo のデバイス周りの E2E テストを追加する

- Priority: Medium
- Created: 2026-06-08
- Polished: 2026-07-10
- Model: DeepSeek V4 Pro
- Branch: feature/add-sumomo-device-tests

## 目的

sumomo のデバイス関連機能（`--list-devices`、実機デバイス指定）に対する E2E テストを追加する。

なお `--fake-capture-device` を使用した映像 / 音声フレームの疎通確認は、既存の `test_sumomo_basic.py` の全テストが `Sumomo` クラスのデフォルト（`fake_capture_device=True`）により既にカバーしているため、本 issue の対象外とする。

## 優先度根拠

デバイス周りはプラットフォーム差分が多くリグレッションが発生しやすい領域だが、現在はテストによる保護がない。ただし既存の basic テストで fake capture device による疎通確認はできているため Medium とする。

## 現状

- `e2e-test/test_sumomo_basic.py` が fake capture device を使った sendonly/recvonly/sendrecv の疎通テストを提供しているが、デバイス列挙や実機デバイス指定のテストはない
- `test/device_list.cpp` がスタンドアロンの C++ デバイス列挙テストとして存在するが、pytest による E2E テストではない
- ハードウェア依存のテスト（`test_sumomo_nvidia_video_codec.py` 等）はモジュールレベルの `pytest.mark.skipif` + 環境変数でスキップ制御している

## 設計方針

### 1. sumomo.py の事前リファクタリング

`test_sumomo_device.py` では `--list-devices` の出力を構造化データとして取得する必要がある。以下のリファクタリングを行う:

- **`get_sumomo_executable_path()`**: `Sumomo._get_sumomo_executable_path()`（`sumomo.py:289`）をモジュールレベル関数として切り出す。`Sumomo.__init__` 内の呼び出しを置き換える
- **`DeviceLists` データクラス**（`sumomo.py:19`）: `--list-devices` の出力をパースして保持するデータクラス。`audio_recording: list[str]`、`audio_playout: list[str]`、`video: list[str]` の 3 フィールドを持つ
- **`get_device_lists()` 関数**（`sumomo.py:27`）: `subprocess.run` で sumomo を `--list-devices` 付きで実行し、標準出力をパースして `DeviceLists` を返す。`timeout=10` を指定
- **`capture_stderr` オプション**（`sumomo.py:345`）: `Sumomo` クラスに追加。`True` にすると別スレッドで stderr を読み取り、プロセス終了後に `self.stderr_output` から取得可能
- **`log_level` オプション**（`sumomo.py:342`）: `Sumomo` クラスに追加。`"verbose" | "info" | "warning" | "error" | None` で sumomo のログレベルを指定可能

### 2. テストファイル構成

`e2e-test/test_sumomo_device.py` 1 ファイルに `--list-devices` の検証と実機デバイスキャプチャのテストの両方を集約する。ファイル分割のメリットが薄く、1 ファイルの方が管理しやすいため。

テストコード側では実行環境を判定する仕組み（skipif や環境変数）は持たない。self-hosted runner での実行制御は CI ワークフロー側で行う。

### 3. `test_sumomo_device.py` のテスト一覧

#### `test_list_devices()`

`subprocess.run` で sumomo を `--list-devices` 付きで実行し、以下を確認する:

1. 終了コードが 0 であること
2. 標準出力に 3 つのセクションヘッダーが含まれること:
   - `=== Available audio input devices ===`
   - `=== Available audio output devices ===`
   - `=== Available video devices ===`

デバイス名の出力形式はプラットフォーム間で異なるため、セクションヘッダーのみを検証する。`timeout=10` を指定する。

#### `test_capture_device(sora_settings, free_port)`

実機カメラ・マイクから取得した映像・音声が送信されることを確認する。`get_device_lists()` で列挙されたデバイスを明示的に指定してキャプチャを行う。映像・音声の同時キャプチャを検証する。

- 確認内容:
  - `get_stats()` が空でないこと
  - `get_outbound_rtp(stats, "video")["frameWidth"]` が正の値であること
  - `get_outbound_rtp(stats, "video")["packetsSent"]` が 0 より大きいこと
  - `get_outbound_rtp(stats, "audio")["packetsSent"]` が 0 より大きいこと
  - `get_transport(stats)["dtlsState"]` が `"connected"` であること

#### `test_audio_recording_device(sora_settings, free_port, audio_recording_device)`

`@pytest.mark.parametrize` で `get_device_lists()` から取得した全音声録音デバイスに対して実行する。`--audio-recording-device` でデバイスを指定し、`--video false` で映像を無効化する。`capture_stderr=True` で stderr をキャプチャし、指定デバイス名がログに含まれることを検証する。

- 確認内容:
  - `get_stats()` が空でないこと
  - `get_outbound_rtp(stats, "audio")["packetsSent"]` が 0 より大きいこと
  - `get_transport(stats)["dtlsState"]` が `"connected"` であること
  - stderr に指定デバイス名が含まれること

#### `test_default_audio_recording_device(sora_settings, free_port)`

録音デバイス名を指定しない場合、デフォルトデバイスが選択されることを確認する。`--video false` を指定。

- 確認内容: 上記 `test_audio_recording_device` と同様（デバイス名検証を除く）

#### `test_invalid_audio_recording_device(sora_settings, free_port)`

存在しない録音デバイス名を指定した場合、デフォルトデバイスにフォールバックすることを確認する。`capture_stderr=True` で警告ログを検証する。

- 確認内容:
  - 接続が成功し stats が取得できること
  - stderr に警告が含まれること

#### `test_audio_playout_device(sora_settings, free_port, free_port2, audio_playout_device)`

`@pytest.mark.parametrize` で `get_device_lists()` から取得した全音声再生デバイスに対して実行する。sendonly + recvonly のペアでテストし、`--audio-playout-device` で受信側の再生デバイスを指定する。

- 確認内容:
  - 受信側の `get_inbound_rtp(stats, "audio")["packetsReceived"]` が 0 より大きいこと
  - 受信側の `get_transport(stats)["dtlsState"]` が `"connected"` であること
- 制約: 本テストはパケット受信の確認までであり、実際の音声出力デバイスへのルーティング検証は E2E テストの限界により行わない

### 4. 実機デバイステスト共通の注意点

- デバイス名はテスト実行時に `get_device_lists()` で動的列挙する。環境変数による事前設定は不要
- キャプチャ失敗時は sumomo の `Sumomo::Run()` が早期リターンし、stats が空になる。テスト側では `get_stats()` が空でないことの確認によってこのケースを検出する
- 実機カメラの解像度は要求解像度と異なる場合があるため、`frameWidth` / `frameHeight` の assert は正の値であることのみとする
- `packetsSent` 等の確認は、接続後に十分な待機時間（`time.sleep(3)` 等）を設けた上で単一の `get_stats()` の値が 0 より大きいことを確認する方式とする（2 時点の差分比較は不要。fake capture device の既存テストと同じ方式）
- `capture_stderr=True` を使用することで、指定デバイスが実際に選択されたかや警告の有無をプログラムから検証可能

### 5. CI ワークフロー側の制御

テストコード側では環境変数によるスキップ制御を行わない。self-hosted runner での実行可否は CI ワークフロー（`.github/workflows/ci.yml`）側でテストターゲットの指定により制御する。

`.env.template` の更新は不要（動的列挙方式に移行したため、デバイス名の環境変数が不要になった）。

## 完了条件

- `e2e-test/sumomo.py` に以下の追加が行われている:
  - `get_sumomo_executable_path()` がモジュールレベル関数として切り出されている
  - `DeviceLists` データクラスが追加されている
  - `get_device_lists()` 関数が追加されている
  - `Sumomo` クラスに `capture_stderr` オプションが追加されている
  - `Sumomo` クラスに `log_level` オプションが追加されている
- `e2e-test/test_sumomo_device.py` が追加され、以下のテストがすべてパスすること:
  - `test_list_devices`（GitHub-hosted runner）
  - `test_capture_device`（self-hosted runner）
  - `test_audio_recording_device`（self-hosted runner、parametrized）
  - `test_default_audio_recording_device`（self-hosted runner）
  - `test_invalid_audio_recording_device`（self-hosted runner）
  - `test_audio_playout_device`（self-hosted runner、parametrized）
- `CHANGES.md` の `## develop` 内 `### misc` セクションに以下の `[ADD]` エントリが追記されている（既に実装済み）:
  - sumomo の E2E テストで `--audio-recording-device` を複数の音声録音デバイスに対して個別に検証するテストを追加する
  - sumomo の E2E テストで録音デバイス未指定時・無効指定時の挙動を検証するテストを追加する
  - sumomo の E2E テストヘルパーに `capture_stderr` オプションを追加する
  - sumomo の E2E テストで `--audio-playout-device` を複数の音声再生デバイスに対して個別に検証するテストを追加する
