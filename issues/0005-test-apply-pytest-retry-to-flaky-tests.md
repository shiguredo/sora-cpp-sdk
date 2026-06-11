# フレーキーなテストに pytest-retry を適用する

- Priority: High
- Created: 2026-06-11
- Polished: 2026-06-11
- Model: Opus 4.7
- Branch: feature/add-pytest-retry-to-flaky-tests

## 目的

E2E テストのうち Sora 側からの WS 切断 (`wscode=4490`) や 5 秒以内の WS graceful close 等によって 1 試行目で失敗する事例が複数観測されており (詳細は 0004)、`pytest-retry` の `pytest.mark.flaky` で吸収する。`pytest-retry` は `test_sumomo_raspberry_pi.py` で実績運用されているが他テストファイルには未適用のため、同様のマーカーを横展開して間欠失敗で schedule CI が連続赤になるのを抑える。

## 優先度根拠

0004 の通り schedule CI が 3 営業日連続で赤になっており、SDK 退行検知の役割が機能していない。Sora Labo 側の安定化を待つよりも、SDK 側でリトライ耐性を上げる方が早く CI を緑に戻せるため High。なお shiguredo-issues 規約「番号順対応」の例外として、本 issue は親 issue 0004 の指示により 0006 / 0007 より先に着手する (0004 着手順序ステップ 1)。

## 現状

- `e2e-test/uv.lock` は存在するが `pytest-retry` のエントリが含まれていない (`grep -c pytest-retry e2e-test/uv.lock` が 0)。`e2e-test/pyproject.toml` の `[dependency-groups].dev` には `pytest-retry` の宣言があるため、`.github/workflows/ci.yml:431` の `uv sync` (`--frozen` 無し) は実行のたびに `uv.lock` を暗黙更新して動作している。本 PR で `uv lock --directory=e2e-test` を実行し `uv.lock` も同期させる。CI 側の `uv sync --frozen` 化は本 issue のスコープ外
- `e2e-test/test_sumomo_raspberry_pi.py:14-21` で `pytestmark = [pytest.mark.skipif(...), pytest.mark.flaky(retries=2, delay=5)]` がリスト形式のモジュールレベル marker として稼働しており、`.github/workflows/ci.yml:503` の `uv run pytest -v -x` 下でも問題なく動作している実績がある。`pytest-retry` の仕様上 `pytest.mark.flaky` の途中 retry 失敗は pytest 内部の fail カウントに加算されず、最終 retry も失敗したときだけ `-x` が発火する
- 他のテストファイルの `pytestmark` 現状は以下のとおりで、いずれも `pytest.mark.flaky` 未適用:

  | ファイル | 現状 |
  |---|---|
  | `e2e-test/test_sumomo_basic.py` | `pytestmark` 自体なし |
  | `e2e-test/test_sumomo_nvidia_video_codec.py` | `pytestmark = pytest.mark.skipif(...)` の単独形式 (`NVIDIA_VIDEO_CODEC`) |
  | `e2e-test/test_sumomo_amd_amf.py` | 同上 (`AMD_AMF`) |
  | `e2e-test/test_sumomo_apple_video_toolbox.py` | 同上 (`APPLE_VIDEO_TOOLBOX`) |
  | `e2e-test/test_sumomo_intel_vpl.py` | 同上 (`INTEL_VPL`) |
  | `e2e-test/test_sumomo_openh264.py` | `pytestmark = [pytest.mark.skipif(...)]` の 1 要素リスト形式 (`OPENH264_PATH`) |

## 設計方針

### 方針案 1: 各テストファイルにモジュールレベル marker を追加する

raspberry_pi と同じ形式で各テストファイルに `pytest.mark.flaky(retries=2, delay=5)` を追加する。テストファイル単位で適用範囲を選択できる。新規テストファイル追加時に付け忘れるリスクがある。

### 方針案 2: pyproject.toml にグローバル設定を追加する

`[tool.pytest.ini_options]` に `retries = 2` / `retry_delay = 5` を追加してリポジトリ全体に retry を効かせる。一括適用できるが、本来安定して通るべきテストにもリトライがかかり CI 実行時間の上振れが生じる。また将来「このテストはリトライ対象から外したい」と判断したときに個別解除の手段が無い。

### 選定: 方針案 1

理由:

- どのテストがフレーキー対象かが diff で明確になり、テストファイル単位の細粒度制御が可能
- 既存の raspberry_pi の運用と統一できる

なお「リトライで本物の退行が隠蔽されるリスク」は方針 1 / 方針 2 のどちらでも発生する (リトライで救済された場合 CI ジョブは success 判定になる)。このリスクの観測責任は 0004 の「表追記の運用ルール」に従って担保する (特に系統 C のフレーム未受信が pytest-retry で連続的に隠蔽されないか継続観測する)。

`retries=2` / `delay=5` (秒) は raspberry_pi の値をそのまま採用する。raspberry_pi (デバイス起因) と本 issue (Sora Labo / ネットワーク起因) で原因系は異なるが、両者とも「一過性事象が数秒で解消される」という仮定の下で実用上機能する値を選ぶ点では同等であり、運用負荷を下げるため値を揃える。仮定が崩れて再フレーキーが続く場合は 0004 の継続観測の中で `delay` を 10 / 20 へ段階的に上げる方針とする。`delay=5` の選択は CI 実行時間の上振れも限定的に収まる (1 件あたりの追加は最悪 `(本体時間 + 5 秒) × 2` で、`test_sumomo_basic.py` 系の本体 20-30 秒に対して許容範囲)。

## 完了条件

- 以下の 6 ファイルにすべて `pytest.mark.flaky(retries=2, delay=5)` がモジュールレベル marker として追加され、それぞれが「解決方法」のファイル別パターンに沿っている
  - `e2e-test/test_sumomo_basic.py`
  - `e2e-test/test_sumomo_nvidia_video_codec.py`
  - `e2e-test/test_sumomo_amd_amf.py`
  - `e2e-test/test_sumomo_apple_video_toolbox.py`
  - `e2e-test/test_sumomo_intel_vpl.py`
  - `e2e-test/test_sumomo_openh264.py`
- 上記 6 ファイル以外 (特に `e2e-test/test_sumomo_raspberry_pi.py`) は本 issue では一切変更しない。raspberry_pi のコメント形式 (`pytestmark` 全体への 1 行コメント) と本 issue の追加形式 (`pytest.mark.flaky` 要素にインラインコメント) の差異も本 issue のスコープ外 (必要なら別 issue で扱う)
- 各ファイルで `pytest.mark.flaky` 要素の直前に日本語インラインコメント (`# Sora 側 WS 切断などのフレーキー対策で retry する (詳細は issues/0004)`) を添えている (AGENTS.md「テストはコメントを重視すること」)
- `e2e-test/uv.lock` に `uv lock --directory=e2e-test` の結果として `pytest-retry` のエントリが追加されている。`pyproject.toml` 側は他の dev 依存と揃えて bare name (`"pytest-retry"`) のままにし、本 issue ではバージョン pin は導入しない
- `CHANGES.md` の `## develop` 配下、既存の `### misc` セクション (現状 `CHANGES.md:88` 付近に存在) に以下の形式で `[ADD]` エントリを追記する。挿入位置は既存 `[CHANGE]` エントリの直後、既存 `[UPDATE]` エントリの直前の物理位置とする (0003 完了条件と同じ位置取り。`### misc` 内既存エントリの並べ替えは本 issue のスコープ外)。担当者ハンドルは PR 作成者のものに書き換える:

  ```
  - [ADD] E2E テストにフレーキー対策として pytest-retry を適用する
    - 対象: test_sumomo_basic.py / test_sumomo_nvidia_video_codec.py / test_sumomo_amd_amf.py / test_sumomo_apple_video_toolbox.py / test_sumomo_intel_vpl.py / test_sumomo_openh264.py
    - retries=2 / delay=5 を pytestmark に追加する
    - @<PR 作成者のハンドル>
  ```

## 解決方法

追加するインラインコメントは全ファイル共通で `# Sora 側 WS 切断などのフレーキー対策で retry する (詳細は issues/0004)` とする。ファイル別の挿入形式は以下のとおり。

- **`test_sumomo_basic.py`** (現状 `pytestmark` なし): `from sumomo import Sumomo` の直後に 1 行空行を挟んで以下を **単独形式** で追加する。挿入後の `pytestmark` と次の `@pytest.mark.parametrize` の間は PEP 8 に従い 2 行空行を確保する (skipif などの併存 marker が無いため、他 5 ファイルとの形式差は意図的に許容する。将来 basic.py に skipif 等が追加されたタイミングでリスト形式に揃える)

  ```python
  # Sora 側 WS 切断などのフレーキー対策で retry する (詳細は issues/0004)
  pytestmark = pytest.mark.flaky(retries=2, delay=5)
  ```

- **`test_sumomo_nvidia_video_codec.py` / `test_sumomo_amd_amf.py` / `test_sumomo_apple_video_toolbox.py` / `test_sumomo_intel_vpl.py`** (現状単独形式の skipif): 既存 `pytestmark = pytest.mark.skipif(...)` をリスト形式に書き換え、`skipif` の後ろに `flaky` を追記する。`skipif` の引数 (`os.environ.get("<NAME>")` と `reason="<NAME> not set in environment"`) は現状のまま触らない (4 ファイル分の `<NAME>` は `NVIDIA_VIDEO_CODEC` / `AMD_AMF` / `APPLE_VIDEO_TOOLBOX` / `INTEL_VPL`)

  ```python
  pytestmark = [
      pytest.mark.skipif(
          not os.environ.get("<既存の環境変数名>"),
          reason="<既存の reason 文言>",
      ),
      # Sora 側 WS 切断などのフレーキー対策で retry する (詳細は issues/0004)
      pytest.mark.flaky(retries=2, delay=5),
  ]
  ```

- **`test_sumomo_openh264.py`** (現状で 1 要素リスト形式): 既存リスト末尾に `flaky` 要素を追記する。`skipif` 要素は触らない

  ```python
  pytestmark = [
      pytest.mark.skipif(
          not os.environ.get("OPENH264_PATH"),
          reason="OPENH264_PATH not set in environment",
      ),
      # Sora 側 WS 切断などのフレーキー対策で retry する (詳細は issues/0004)
      pytest.mark.flaky(retries=2, delay=5),
  ]
  ```
