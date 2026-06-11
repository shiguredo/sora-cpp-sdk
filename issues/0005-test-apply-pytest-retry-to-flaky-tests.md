# フレーキーなテストに pytest-retry を適用する

- Priority: High
- Created: 2026-06-11
- Polished: {YYYY-MM-DD}
- Model: Opus 4.7
- Branch: feature/add-pytest-retry-to-flaky-tests

## 目的

E2E テストのうち Sora Labo 起因の `wscode=4490` 切断や 5 秒以内の WebSocket 切断によって 1 試行目で失敗するテストが複数観測されている (0004 参照)。`pytest-retry` はすでに `e2e-test/pyproject.toml` の dev dependency に含まれており、`test_sumomo_raspberry_pi.py` ではモジュールレベルで `pytest.mark.flaky(retries=2, delay=5)` が適用済み。同様のマーカーを他のテストファイルにも展開し、間欠失敗で schedule CI が連続赤になるのを抑える。

## 優先度根拠

0004 の通り schedule CI が 3 日連続で赤になっており、SDK 退行検知の役割が機能していない。Sora Labo 側の安定化を待つよりも、SDK 側でリトライ耐性を上げる方が早く CI を緑に戻せるため High。

## 現状

- `e2e-test/pyproject.toml:12` の dev dependency に `pytest-retry` がある
- `e2e-test/test_sumomo_raspberry_pi.py:14-21` で `pytest.mark.flaky(retries=2, delay=5)` がモジュールレベル marker として使われている
- 他のテストファイル (`test_sumomo_basic.py` / `test_sumomo_nvidia_video_codec.py` / `test_sumomo_amd_amf.py` / `test_sumomo_apple_video_toolbox.py` / `test_sumomo_intel_vpl.py` / `test_sumomo_openh264.py`) には flaky マーカーがついていない
- 結果として raspberry_pi 以外は 1 度失敗すると即赤になる

## 設計方針

### 方針案 1: 各テストファイルにモジュールレベル marker を追加する

raspberry_pi と同じ形式で各テストファイル先頭に `pytest.mark.flaky(retries=2, delay=5)` を追加する。明示的でわかりやすいが、新規テストファイル追加時に付け忘れるリスクがある。

### 方針案 2: pyproject.toml にグローバル設定を追加する

`[tool.pytest.ini_options]` に `retries = 2` / `retry_delay = 5` を追加してリポジトリ全体に retry を効かせる。一括適用できるが、本来安定して通るべきテストもリトライされてしまい、本物の退行に気付くのが遅れるリスクがある。

### 選定: 方針案 1

理由:

- どのテストがフレーキー対象として扱われているかが diff で明確になる
- リトライ動作の有無をテストファイル単位で個別調整できる
- 「本来フレーキーであるべきではない」と判断した場合に当該マーカーを外すだけで戻せる
- 既存の raspberry_pi の運用と統一できる

`retries=2` / `delay=5` は raspberry_pi と揃える。

## 完了条件

- 以下のテストファイルにモジュールレベルマーカーとして `pytest.mark.flaky(retries=2, delay=5)` が追加されている:
  - `e2e-test/test_sumomo_basic.py`
  - `e2e-test/test_sumomo_nvidia_video_codec.py`
  - `e2e-test/test_sumomo_amd_amf.py`
  - `e2e-test/test_sumomo_apple_video_toolbox.py`
  - `e2e-test/test_sumomo_intel_vpl.py`
  - `e2e-test/test_sumomo_openh264.py`
- 既存のモジュールレベル marker (`pytest.mark.skipif` 等) と共存できている (リスト形式での併記)
- schedule CI で 1 試行目に失敗してもリトライで救済される動作が実機で確認できている
- `CHANGES.md` の `## develop` に `[ADD]` エントリを追記する

## 解決方法

raspberry_pi で実績のある以下の形式を、対象テストファイルにモジュールレベルで追記する。

既存のモジュールレベル marker が無いファイル:

```python
import pytest

pytestmark = pytest.mark.flaky(retries=2, delay=5)
```

既存のモジュールレベル marker (`pytest.mark.skipif` 等) があるファイルは raspberry_pi 同様にリスト形式に変更する:

```python
pytestmark = [
    pytest.mark.skipif(...),
    pytest.mark.flaky(retries=2, delay=5),
]
```

`CHANGES.md` の `## develop` に `[ADD]` エントリを追記する (AGENTS.md の種別順序に従い適切な位置に配置する)。
