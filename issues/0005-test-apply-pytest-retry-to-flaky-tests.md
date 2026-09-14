# フレーキーなテストに pytest-retry を適用する
- Polish-Timeout: 2026-09-14


- Priority: Medium
- Created: 2026-06-11
- Completed: {YYYY-MM-DD}
- Model: Opus 4.7
- Branch: feature/add-pytest-retry-to-flaky-tests
- Polished: 2026-09-14
## 目的

E2E テストのうち Sora Labo 環境に依存するテストは、Sora 側からの WS 切断 (`wscode=4490`) やネットワーク不安定による間欠失敗を起こす可能性がある。`pytest-retry` の `pytest.mark.flaky` は `test_sumomo_raspberry_pi.py` で実績運用されており、同様のマーカーを他テストファイルにも横展開して予防的に間欠失敗耐性を上げる。

## 優先度根拠

Sora Labo 側の事象は SDK チーム管理外であり、再発リスクは常に存在する。実装コストが極小（対象 8 ファイルのマーカー追加のみ。`uv.lock` は既に同期済みで変更不要）であること、および retries=2 では本物の持続的退行は隠蔽されないことから、予防的保険として価値があるため Medium。raspberry_pi ですでに稼働実績のあるパラメータを流用するため、新規の設計リスクも発生しない。

## 現状

- `e2e-test/uv.lock` は既に `pytest-retry` のエントリ（1.7.0）を含んでおり、`e2e-test/pyproject.toml` の `[dependency-groups].dev` の `pytest-retry~=1.7.0` と同期済み（コミット `8f6bcd5c`「E2E テストの依存を更新し Python 3.14 に引き上げる」で同期）。`.github/workflows/ci.yml` の `uv sync` は `--frozen` 無しだが、lock と pyproject.toml が一致しているため実行のたびの暗黙更新は発生しない。CI 側の `uv sync --frozen` 化は本 issue のスコープ外
- `e2e-test/test_sumomo_raspberry_pi.py` で `pytestmark = [pytest.mark.skipif(...), pytest.mark.flaky(retries=2, delay=5)]` がリスト形式のモジュールレベル marker として稼働しており、`uv run pytest -v -x` 下でも問題なく動作している実績がある。`pytest-retry` の仕様上 `pytest.mark.flaky` の途中 retry 失敗は pytest 内部の fail カウントに加算されず、最終 retry も失敗したときだけ `-x` が発火する
- 他のテストファイルの `pytestmark` 現状は以下のとおりで、いずれも `pytest.mark.flaky` 未適用:

  | ファイル | 現状 |
  |---|---|
  | `e2e-test/test_sumomo_basic.py` | `pytestmark` 自体なし |
  | `e2e-test/test_sumomo_nvidia_video_codec.py` | `pytestmark = pytest.mark.skipif(...)` の単独形式 (`NVIDIA_VIDEO_CODEC`) |
  | `e2e-test/test_sumomo_amd_amf.py` | 同上 (`AMD_AMF`) |
  | `e2e-test/test_sumomo_apple_video_toolbox.py` | 同上 (`APPLE_VIDEO_TOOLBOX`) |
  | `e2e-test/test_sumomo_intel_vpl.py` | 同上 (`INTEL_VPL`) |
  | `e2e-test/test_sumomo_openh264.py` | `pytestmark = [pytest.mark.skipif(...)]` の 1 要素リスト形式 (`OPENH264_PATH`) |
  | `e2e-test/test_sumomo_tls_verification.py` | `pytestmark` 自体なし |
  | `e2e-test/test_sumomo_device.py` | `pytestmark` 自体なし |
- `e2e-test/test_sumomo_device.py` は実機キャプチャデバイスを要求し self-hosted runner でのみ実行されるが、`test_capture_device` などのテストは Sora Labo に接続する。0004 の観測表で self-hosted の nvidia_video_codec ジョブが `wscode=4490` で失敗しており、self-hosted でも Sora Labo 起因の間欠失敗は起こり得るため、本 issue の対象に含める

## 設計方針

### 方針案 1: 各テストファイルにモジュールレベル marker を追加する

raspberry_pi と同じ形式で各テストファイルに `pytest.mark.flaky(retries=2, delay=5)` を追加する。テストファイル単位で適用範囲を選択できる。新規テストファイル追加時に付け忘れるリスクがある。
### 方針案 2: pyproject.toml にグローバル設定を追加する

`[tool.pytest.ini_options]` に `retries = 2` / `retry_delay = 5` を追加してリポジトリ全体に retry を効かせる。一括適用できるが、本来安定して通るべきテストにもリトライがかかり CI 実行時間の上振れが生じる。個別に `condition=False` で無効化は可能だが、テストファイルごとに明示的な無効化宣言が必要になり管理が煩雑になる（なお `retries=0` は pytest-retry 1.7.0 ではリトライ 1 回が残るため無効化手段にならない）。また将来テストファイルを追加するたびに retry 対象かどうかの判断が暗黙化する。

### 選定: 方針案 1

理由:

- どのテストがフレーキー対象かが diff で明確になり、テストファイル単位の細粒度制御が可能
- 既存の raspberry_pi の運用と統一できる

なお「リトライで本物の退行が隠蔽されるリスク」は方針 1 / 方針 2 のどちらでも発生する（リトライで救済された場合 CI ジョブは success 判定になる）。ただし retries=2（計 3 試行）では本物の持続的退行は全試行で失敗するため検出可能であり、間欠的退行のエッジケースのみがリスクとなる。

`retries=2` / `delay=5`（秒）は raspberry_pi の値をそのまま採用する。raspberry_pi（デバイス起因）と本 issue（Sora Labo / ネットワーク起因）で原因系は異なるが、両者とも「一過性事象が数秒で解消される」という仮定の下で実用上機能する値を選ぶ点では同等であり、運用負荷を下げるため値を揃える。再フレーキーが続く場合は `delay` を 10 / 20 へ段階的に上げる方針とする。`delay=5` の選択は CI 実行時間の上振れも限定的に収まる（1 件あたりの追加は最悪 `(本体時間 + 5 秒) × 2` で、`test_sumomo_basic.py` 系の本体 20-30 秒に対して許容範囲）。

## 完了条件

- 以下の 8 ファイルにすべて `pytest.mark.flaky(retries=2, delay=5)` がモジュールレベル marker として追加され、それぞれが「解決方法」のファイル別パターンに沿っている
  - `e2e-test/test_sumomo_basic.py`
  - `e2e-test/test_sumomo_nvidia_video_codec.py`
  - `e2e-test/test_sumomo_amd_amf.py`
  - `e2e-test/test_sumomo_apple_video_toolbox.py`
  - `e2e-test/test_sumomo_intel_vpl.py`
  - `e2e-test/test_sumomo_openh264.py`
  - `e2e-test/test_sumomo_tls_verification.py`
  - `e2e-test/test_sumomo_device.py`
- 上記 8 ファイル以外（特に `e2e-test/test_sumomo_raspberry_pi.py`）は本 issue では一切変更しない。raspberry_pi のコメント形式（`pytestmark` 全体への 1 行コメント）と本 issue の追加形式（`pytest.mark.flaky` 要素へのインラインコメント）の差異も本 issue のスコープ外（必要なら別 issue で扱う）
- 各ファイルで `pytest.mark.flaky` 要素の直前に日本語インラインコメント（`# Sora Labo 側の一時的な WS 切断やネットワーク不安定による間欠失敗を吸収する`）を添えている（AGENTS.md「テストはコメントを重視すること」）
- `e2e-test/uv.lock` に `pytest-retry` のエントリが含まれている（既に同期済みのため `uv lock` の再実行は不要。`pyproject.toml` 側は他の dev 依存と同形式の `pytest-retry~=1.7.0` のまま変更しない）
- `CHANGES.md` の `## develop` 配下、`### misc` セクションに以下の形式で `[ADD]` エントリを追記する。`### misc` セクション内では凡例順（CHANGE → ADD → UPDATE → FIX）を尊重し `[ADD]` は `[CHANGE]` の直後、先頭の `[UPDATE]` の直前に挿入する。`### misc` 内既存エントリの並べ替えは本 issue のスコープ外。担当者ハンドル `@<担当者>` は PR 作成者のものに書き換える:

  ```
  - [ADD] E2E テストにフレーキー対策として pytest-retry を適用する
    - 対象: test_sumomo_basic.py / test_sumomo_nvidia_video_codec.py / test_sumomo_amd_amf.py / test_sumomo_apple_video_toolbox.py / test_sumomo_intel_vpl.py / test_sumomo_openh264.py / test_sumomo_tls_verification.py / test_sumomo_device.py
    - retries=2 / delay=5 を pytestmark に追加する
    - @<担当者>
  ```
- 以下の検証を実施し、すべてパスしている:
  - `e2e-test/uv.lock` に `pytest-retry` のエントリが含まれることを確認（`grep pytest-retry e2e-test/uv.lock`）
  - 各対象ファイルで `uv run --directory=e2e-test pytest <ファイル名> --collect-only` を実行し、`pytest.mark.flaky` マーカーが認識されることを確認（`test_sumomo_device.py` は `pytest_generate_tests` が収集時に `get_device_lists()` = `sumomo --list-devices` を実行するため、ビルド済み sumomo が必要）
  - ローカル環境で少なくとも 1 つの対象テストファイルのケースを CODEBASE.md の実行形式（`uv run --directory=e2e-test pytest <test_file>::<test_case> -v -s --timeout=60`）で実行し、マーカーが機能することを確認

## 解決方法

追加するインラインコメントは全ファイル共通で `# Sora Labo 側の一時的な WS 切断やネットワーク不安定による間欠失敗を吸収する` とする。ファイル別の挿入形式は以下のとおり。

- **`test_sumomo_basic.py`**（現状 `pytestmark` なし）: `from sumomo import Sumomo` の直後に 1 行空行を挟んで以下を **単独形式** で追加する。既存の空行（`from sumomo import Sumomo` と次の `@pytest.mark.parametrize` の間の 2 行）は残したまま、その間に挿入することで、結果として `pytestmark` 代入文と次の `@pytest.mark.parametrize` の間には 2 行空行が確保される。skipif などの併存 marker が無いため、skipif 併存ファイルとの形式差は意図的に許容する（将来 basic.py に skipif 等が追加されたタイミングでリスト形式に揃える）

  ```python
  # Sora Labo 側の一時的な WS 切断やネットワーク不安定による間欠失敗を吸収する
  pytestmark = pytest.mark.flaky(retries=2, delay=5)
  ```

- **`test_sumomo_tls_verification.py`**（現状 `pytestmark` なし）: `from sumomo import Sumomo` の直後に 1 行空行を挟んで basic と同じ **単独形式** で追加する（`ISRG_ROOT_X1_PEM` 定数より前）。併存 marker が無いため形式差は basic と同様に意図的に許容する

- **`test_sumomo_device.py`**（現状 `pytestmark` なし）: `from sumomo import Sumomo, get_device_lists, get_sumomo_executable_path` の直後に 1 行空行を挟んで basic と同じ **単独形式** で追加する。併存 marker が無いため形式差は basic と同様に意図的に許容する

- **`test_sumomo_nvidia_video_codec.py` / `test_sumomo_amd_amf.py` / `test_sumomo_apple_video_toolbox.py` / `test_sumomo_intel_vpl.py`**（現状単独形式の skipif）: 既存 `pytestmark = pytest.mark.skipif(...)` をリスト形式に書き換え、`skipif` の後ろに `flaky` を追記する。`skipif` の引数（`os.environ.get("<NAME>")` と `reason="<NAME> not set in environment"`）は現状のまま触らない（4 ファイル分の `<NAME>` は `NVIDIA_VIDEO_CODEC` / `AMD_AMF` / `APPLE_VIDEO_TOOLBOX` / `INTEL_VPL`）

  ```python
  pytestmark = [
      pytest.mark.skipif(
          not os.environ.get("<既存の環境変数名>"),
          reason="<既存の reason 文言>",
      ),
      # Sora Labo 側の一時的な WS 切断やネットワーク不安定による間欠失敗を吸収する
      pytest.mark.flaky(retries=2, delay=5),
  ]
  ```

- **`test_sumomo_openh264.py`**（現状で 1 要素リスト形式）: 既存リスト末尾に `flaky` 要素を追記する。`skipif` 要素は触らない

  ```python
  pytestmark = [
      pytest.mark.skipif(
          not os.environ.get("OPENH264_PATH"),
          reason="OPENH264_PATH not set in environment",
      ),
      # Sora Labo 側の一時的な WS 切断やネットワーク不安定による間欠失敗を吸収する
      pytest.mark.flaky(retries=2, delay=5),
  ]
  ```

