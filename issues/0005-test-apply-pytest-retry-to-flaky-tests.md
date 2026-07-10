# フレーキーなテストに pytest-retry を適用する

- Priority: Medium
- Created: 2026-06-11
- Polished: 2026-07-10
- Model: Opus 4.7
- Branch: feature/add-pytest-retry-to-flaky-tests

## 目的

E2E テストのうち Sora Labo 環境に依存するテストは、Sora 側からの WS 切断 (`wscode=4490`) やネットワーク不安定による間欠失敗を起こす可能性がある。`pytest-retry` の `pytest.mark.flaky` は `test_sumomo_raspberry_pi.py` で実績運用されており、同様のマーカーを他テストファイルにも横展開して予防的に間欠失敗耐性を上げる。

## 優先度根拠

Sora Labo 側の事象は SDK チーム管理外であり、再発リスクは常に存在する。実装コストが極小（6 ファイルのマーカー追加、`uv.lock` 同期のみ）であること、および retries=2 では本物の持続的退行は隠蔽されないことから、予防的保険として価値があるため Medium。raspberry_pi ですでに稼働実績のあるパラメータを流用するため、新規の設計リスクも発生しない。

## 現状

- `e2e-test/uv.lock` は存在するが `pytest-retry` のエントリが含まれていない。`e2e-test/pyproject.toml` の `[dependency-groups].dev` には `pytest-retry` の宣言があるため、`.github/workflows/ci.yml` の `uv sync` は実行のたびに `uv.lock` を暗黙更新して動作している。本 PR でプロジェクトルートから `uv lock --directory=e2e-test` を実行し `uv.lock` も同期させる。CI 側の `uv sync --frozen` 化は本 issue のスコープ外
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
- `e2e-test/test_sumomo_device.py` は実機キャプチャデバイスを要求し self-hosted runner でのみ実行されるため、Sora Labo の不安定性の影響を受けにくく、本 issue の対象外とする

## 設計方針

### 方針案 1: 各テストファイルにモジュールレベル marker を追加する

raspberry_pi と同じ形式で各テストファイルに `pytest.mark.flaky(retries=2, delay=5)` を追加する。テストファイル単位で適用範囲を選択できる。新規テストファイル追加時に付け忘れるリスクがある。
### 方針案 2: pyproject.toml にグローバル設定を追加する

`[tool.pytest.ini_options]` に `retries = 2` / `retry_delay = 5` を追加してリポジトリ全体に retry を効かせる。一括適用できるが、本来安定して通るべきテストにもリトライがかかり CI 実行時間の上振れが生じる。個別に `retries=0` で無効化は可能だが、テストファイルごとに明示的な無効化宣言が必要になり管理が煩雑になる。また将来テストファイルを追加するたびに retry 対象かどうかの判断が暗黙化する。

### 選定: 方針案 1

理由:

- どのテストがフレーキー対象かが diff で明確になり、テストファイル単位の細粒度制御が可能
- 既存の raspberry_pi の運用と統一できる

なお「リトライで本物の退行が隠蔽されるリスク」は方針 1 / 方針 2 のどちらでも発生する（リトライで救済された場合 CI ジョブは success 判定になる）。ただし retries=2（計 3 試行）では本物の持続的退行は全試行で失敗するため検出可能であり、間欠的退行のエッジケースのみがリスクとなる。

`retries=2` / `delay=5`（秒）は raspberry_pi の値をそのまま採用する。raspberry_pi（デバイス起因）と本 issue（Sora Labo / ネットワーク起因）で原因系は異なるが、両者とも「一過性事象が数秒で解消される」という仮定の下で実用上機能する値を選ぶ点では同等であり、運用負荷を下げるため値を揃える。再フレーキーが続く場合は `delay` を 10 / 20 へ段階的に上げる方針とする。`delay=5` の選択は CI 実行時間の上振れも限定的に収まる（1 件あたりの追加は最悪 `(本体時間 + 5 秒) × 2` で、`test_sumomo_basic.py` 系の本体 20-30 秒に対して許容範囲）。

## 完了条件

- 以下の 6 ファイルにすべて `pytest.mark.flaky(retries=2, delay=5)` がモジュールレベル marker として追加され、それぞれが「解決方法」のファイル別パターンに沿っている
  - `e2e-test/test_sumomo_basic.py`
  - `e2e-test/test_sumomo_nvidia_video_codec.py`
  - `e2e-test/test_sumomo_amd_amf.py`
  - `e2e-test/test_sumomo_apple_video_toolbox.py`
  - `e2e-test/test_sumomo_intel_vpl.py`
  - `e2e-test/test_sumomo_openh264.py`
- 上記 6 ファイル以外（特に `e2e-test/test_sumomo_raspberry_pi.py`、`e2e-test/test_sumomo_device.py`）は本 issue では一切変更しない。raspberry_pi のコメント形式（`pytestmark` 全体への 1 行コメント）と本 issue の追加形式（`pytest.mark.flaky` 要素へのインラインコメント）の差異も本 issue のスコープ外（必要なら別 issue で扱う）
- 各ファイルで `pytest.mark.flaky` 要素の直前に日本語インラインコメント（`# Sora Labo 側の一時的な WS 切断やネットワーク不安定による間欠失敗を吸収する`）を添えている（AGENTS.md「テストはコメントを重視すること」）
- `e2e-test/uv.lock` にプロジェクトルートからの `uv lock --directory=e2e-test` の結果として `pytest-retry` のエントリが追加されている。`pyproject.toml` 側は他の dev 依存と揃えて bare name（`"pytest-retry"`）のままにし、本 issue ではバージョン pin は導入しない
- `CHANGES.md` の `## develop` 配下、`### misc` セクションに以下の形式で `[ADD]` エントリを追記する。`### misc` セクション内では凡例順（CHANGE → ADD → UPDATE → FIX）を尊重し `[ADD]` は `[CHANGE]` の直後、先頭の `[UPDATE]` の直前に挿入する。`### misc` 内既存エントリの並べ替えは本 issue のスコープ外。担当者ハンドル `@<担当者>` は PR 作成者のものに書き換える:

  ```
  - [ADD] E2E テストにフレーキー対策として pytest-retry を適用する
    - 対象: test_sumomo_basic.py / test_sumomo_nvidia_video_codec.py / test_sumomo_amd_amf.py / test_sumomo_apple_video_toolbox.py / test_sumomo_intel_vpl.py / test_sumomo_openh264.py
    - retries=2 / delay=5 を pytestmark に追加する
    - @<担当者>
  ```
- 以下の検証を実施し、すべてパスしている:
  - `uv lock --directory=e2e-test` 後、`git diff e2e-test/uv.lock` で `pytest-retry` エントリの追加を確認
  - 各対象ファイルで `pytest --collect-only` を実行し、`pytest.mark.flaky` マーカーが認識されることを確認
  - ローカル環境で少なくとも 1 つの対象テストファイルを `uv run pytest -v` で実行し、マーカーが機能することを確認

## 解決方法

追加するインラインコメントは全ファイル共通で `# Sora Labo 側の一時的な WS 切断やネットワーク不安定による間欠失敗を吸収する` とする。ファイル別の挿入形式は以下のとおり。

- **`test_sumomo_basic.py`**（現状 `pytestmark` なし）: `from sumomo import Sumomo`（8 行目）の直後に 1 行空行を挟んで以下を **単独形式** で追加する。既存の空行（9-10 行目）は残したまま、その間に挿入することで、結果として `pytestmark` 代入文と次の `@pytest.mark.parametrize` の間には 2 行空行が確保される。skipif などの併存 marker が無いため、他 5 ファイルとの形式差は意図的に許容する（将来 basic.py に skipif 等が追加されたタイミングでリスト形式に揃える）

  ```python
  # Sora Labo 側の一時的な WS 切断やネットワーク不安定による間欠失敗を吸収する
  pytestmark = pytest.mark.flaky(retries=2, delay=5)
  ```

- **`test_sumomo_nvidia_video_codec.py` / `test_sumomo_amd_amf.py` / `test_sumomo_apple_video_toolbox.py` / `test_sumomo_intel_vpl.py`**（現状単独形式の skipif、21 行目付近）: 既存 `pytestmark = pytest.mark.skipif(...)` をリスト形式に書き換え、`skipif` の後ろに `flaky` を追記する。`skipif` の引数（`os.environ.get("<NAME>")` と `reason="<NAME> not set in environment"`）は現状のまま触らない（4 ファイル分の `<NAME>` は `NVIDIA_VIDEO_CODEC` / `AMD_AMF` / `APPLE_VIDEO_TOOLBOX` / `INTEL_VPL`）

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

- **`test_sumomo_openh264.py`**（現状で 1 要素リスト形式、12 行目付近）: 既存リスト末尾に `flaky` 要素を追記する。`skipif` 要素は触らない

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

