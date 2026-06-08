# sumomo のデバイス周りの E2E テストを追加する

- Priority: Medium
- Created: 2026-06-08
- Polished: 2026-06-08
- Model: DeepSeek V4 Pro
- Branch: feature/add-sumomo-device-tests

## 目的

sumomo のデバイス関連機能 (`--list-devices`、実機デバイス指定) に対する E2E テストが存在しないため、pytest によるテストを追加する。

なお `--fake-capture-device` を使用した映像 / 音声フレームの疎通確認は、既存の `test_sumomo_basic.py` の全テストが `Sumomo` クラスのデフォルト (`fake_capture_device=True`) により既にカバーしているため、本 issue の対象外とする。

## 優先度根拠

デバイス周りはプラットフォーム差分が多くリグレッションが発生しやすい領域だが、現在はテストによる保護がない。ただし既存の basic テストで fake capture device による疎通確認はできているため Medium とする。

## 現状

- `e2e-test/test_sumomo_basic.py` が fake capture device を使った sendonly/recvonly/sendrecv の疎通テストを提供しているが、デバイス列挙や実機デバイス指定のテストはない
- `test/device_list.cpp` がスタンドアロンの C++ デバイス列挙テストとして存在するが、pytest による E2E テストではない
- ハードウェア依存のテスト (`test_sumomo_nvidia_video_codec.py` 等 ) はモジュールレベルの `pytest.mark.skipif` + 環境変数でスキップ制御している

## 設計方針

### 1. sumomo.py の事前リファクタリング

`test_sumomo_device.py` では `--list-devices` 実行のために sumomo バイナリのパスが必要だが、`Sumomo` クラスはシグナリング接続を前提としており `--list-devices` 用途には過剰である。そのため以下のリファクタリングを事前に行う:

- `Sumomo._get_sumomo_executable_path()` (`sumomo.py:289`) をモジュールレベルの関数 `get_sumomo_executable_path()` として切り出す
- `Sumomo.__init__` 内の `self._get_sumomo_executable_path()` 呼び出しを `get_sumomo_executable_path()` に置き換える
- 既存のテストコードには影響しない（内部リファクタリングのため）

### 2. テストファイル構成

以下の 2 ファイルを新規作成する:

| ファイル | 対象 | 実行環境 | 必要な環境変数 |
|---|---|---|---|
| `e2e-test/test_sumomo_device.py` | `--list-devices` の検証 | GitHub-hosted runner で実行可能 | 不要 |
| `e2e-test/test_sumomo_device_hardware.py` | 実機デバイスを使ったキャプチャテスト | self-hosted runner のみ | `SELF_HOSTED_RUNNER`、`TEST_SIGNALING_URL` 等、デバイス名指定用の環境変数 |

### 3. `test_sumomo_device.py`

`--list-devices` はシグナリング接続が不要であり、また HTTP サーバー起動前にプロセスが終了する (`sumomo.cpp` で `ListDevices()` 実行後 `return 0` する) ため、`Sumomo` クラスを使わず `subprocess.run` で直接実行する。実行ファイルのパスは `from sumomo import get_sumomo_executable_path` で取得する。

#### `test_list_devices`

`subprocess.run` で sumomo を `--list-devices` 付きで実行し、以下を確認する:

1. 終了コードが 0 であること
2. 標準出力に 3 つのセクションヘッダーが含まれること:
   - `=== Available audio input devices ===`
   - `=== Available audio output devices ===`
   - `=== Available video devices ===`

デバイスが存在しない環境ではセクションヘッダーのみ出力され、各セクションの内容は `(none)` になる。そのため、デバイス数の検証は行わない。

プラットフォーム間でデバイス名の出力形式が異なるが (Linux では V4L2 形式、macOS/Windows では `DeviceList::EnumVideoCapturer()` の形式)、セクションヘッダーのみを検証することでプラットフォーム差を吸収する。

注意点:
- `subprocess.run` には `timeout=10` を指定し、デバイス列挙がハングした場合にテストが永遠にブロックされないようにする
- Linux 環境で V4L2 列挙が失敗した場合、`=== Available video devices ===` ヘッダーは出力されるが `Failed to enumerate video devices` が stderr に出力される。セクションヘッダーの確認のみではこのケースを検出できないが、終了コードが 0 以外になる場合はテストが失敗するためカバーされる

### 4. `test_sumomo_device_hardware.py`

self-hosted runner でのみ実行するテスト。モジュールレベルのスキップ制御:

```python
import os
import pytest

pytestmark = pytest.mark.skipif(
    not os.environ.get("SELF_HOSTED_RUNNER"),
    reason="SELF_HOSTED_RUNNER not set in environment",
)
```

デバイス名は以下の環境変数から取得し、未設定の場合は各テスト内で個別に `pytest.skip()` する:

| 環境変数 | 用途 |
|---|---|
| `TEST_VIDEO_DEVICE_NAME` | 実カメラのデバイス名 |
| `TEST_AUDIO_RECORDING_DEVICE_NAME` | 実マイクのデバイス名 |
| `TEST_AUDIO_PLAYOUT_DEVICE_NAME` | 実スピーカーのデバイス名 |

テストでは既存テストのパターンに従い `from helper import get_outbound_rtp, get_inbound_rtp, get_transport` を使用して stats から必要な情報を取得する。各ヘルパー関数は stats に対象のエントリが存在しない場合 `None` を返すため、assert の前に `is not None` の確認を行う。

#### `test_enumerate_devices`

`subprocess.run` で sumomo を `--list-devices` 付きで実行し、映像 / 音声デバイスが 1 つ以上列挙されることを確認する。具体的には、標準出力の各セクションヘッダーの直後に `  [0]` から始まるエントリ行が存在することを確認する（`(none)` 以外の出力があることの確認）。`--list-devices` はデバイス名の環境変数を必要としないため、追加の skip 条件は不要。パス解決は `get_sumomo_executable_path()` を使用し、`timeout=10` を指定する。

#### `test_real_video_device_capture(sora_settings, free_port)`

- `Sumomo` クラスを使用し、sendonly で接続する
- `fake_capture_device=False` を明示的に指定する（デフォルトは `True` のため）
- `--video-device` に `TEST_VIDEO_DEVICE_NAME` を指定する
- `resolution` は `VGA` (640x480) を明示指定する（デフォルト値への暗黙依存を避けるため）
- 環境変数 `TEST_VIDEO_DEVICE_NAME` が未設定の場合は `pytest.skip()` する
- 確認内容:
  - `get_stats()` が空でないこと
  - `get_outbound_rtp(stats, "video")["frameWidth"]` が正の値であること
  - `get_outbound_rtp(stats, "video")["frameHeight"]` が正の値であること
  - `get_outbound_rtp(stats, "video")["packetsSent"]` が 0 より大きいこと
  - `get_transport(stats)["dtlsState"]` が `"connected"` であること

#### `test_real_audio_recording_device_capture(sora_settings, free_port)`

- `Sumomo` クラスを使用し、sendonly で接続する
- `fake_capture_device=False` を明示的に指定する
- `--audio-recording-device` に `TEST_AUDIO_RECORDING_DEVICE_NAME` を指定する
- `--video false` を指定する（映像は不要）
- 環境変数 `TEST_AUDIO_RECORDING_DEVICE_NAME` が未設定の場合は `pytest.skip()` する
- 確認内容:
  - `get_stats()` が空でないこと
  - `get_outbound_rtp(stats, "audio")["packetsSent"]` が 0 より大きいこと
  - `get_transport(stats)["dtlsState"]` が `"connected"` であること

#### `test_real_audio_playout_device(sora_settings, port_allocator)`

- 受信側 (recvonly) にパケットを送るため、送信側 (sendonly) と受信側の 2 つの `Sumomo` インスタンスを使用する
- 送信側 : `audio=True`, `fake_capture_device=True`（デフォルト）、sendonly
- 受信側 : `audio=True`, `fake_capture_device=False`, `--audio-playout-device` に `TEST_AUDIO_PLAYOUT_DEVICE_NAME` を指定、recvonly
- 環境変数 `TEST_AUDIO_PLAYOUT_DEVICE_NAME` が未設定の場合は `pytest.skip()` する
- 確認内容:
  - 受信側の `get_inbound_rtp(stats, "audio")["packetsReceived"]` が 0 より大きいこと
  - 受信側の `get_transport(stats)["dtlsState"]` が `"connected"` であること
- 制約 : 本テストはパケット受信の確認までであり、実際の音声出力デバイスへのルーティング検証は E2E テストの限界により行わない

### 5. 実機デバイステスト共通の注意点

- キャプチャ失敗時 (`CreateCameraDeviceCapturer` が `nullptr` を返した場合)、sumomo の `Sumomo::Run()` が早期リターンし、HTTP サーバーが起動しないまたは stats が空になる。テスト側では `get_stats()` が空でないことの確認によってこのケースを検出する。stderr の内容は `Sumomo` クラスが `subprocess.Popen` を `stderr=None`（親プロセスに継承）で起動しているため、テストコードからプログラムで読み取ることはできない。
- 実機カメラの解像度は要求解像度と異なる場合があるため、`frameWidth` / `frameHeight` の assert は正の値であることのみとする
- `packetsSent` 等の確認は、接続後に十分な待機時間 (`time.sleep(3)` 等 ) を設けた上で単一の `get_stats()` の値が 0 より大きいことを確認する方式とする（2 時点の差分比較は不要。fake capture device の既存テストと同じ方式）

### 6. `.env.template` の更新

既存エントリはそのまま維持し、以下の 4 行を追記する。`SELF_HOSTED_RUNNER` 行は self-hosted runner でのみ設定する（GitHub-hosted runner の `.env` では削除またはコメントアウトする）。

```
SELF_HOSTED_RUNNER=1
TEST_VIDEO_DEVICE_NAME=
TEST_AUDIO_RECORDING_DEVICE_NAME=
TEST_AUDIO_PLAYOUT_DEVICE_NAME=
```

既存のハードウェアテスト用環境変数 (`NVIDIA_VIDEO_CODEC` 等 ) の `.env.template` への追加は本 issue のスコープ外とする。

## 完了条件

- `e2e-test/sumomo.py` から `get_sumomo_executable_path()` がモジュールレベル関数として切り出されている
- `e2e-test/test_sumomo_device.py` が追加され、GitHub-hosted runner 上で `test_list_devices` がパスする
- `e2e-test/test_sumomo_device_hardware.py` が追加され、self-hosted runner 上で全テストがパスする
- `e2e-test/.env.template` に上記 4 行が追記されている
- `CHANGES.md` の `## develop` 内 `### misc` セクションに `[ADD]` エントリを追記する（AGENTS.md の種別順序 `CHANGE → ADD → UPDATE → FIX` に従い、`[CHANGE]` エントリの後、`[UPDATE]` エントリの前に配置する）
