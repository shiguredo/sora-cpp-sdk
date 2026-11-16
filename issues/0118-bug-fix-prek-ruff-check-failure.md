# prek run の ruff check がリポジトリ全体で 114 件失敗し、自動修正できない問題を解消する

- Created: 2026-11-16
- Completed: {YYYY-MM-DD}
- Branch: feature/fix-prek-ruff-check
- Polished: {YYYY-MM-DD}

## 目的

`prek run --all-files` の最後に実行される `ruff check` が、リポジトリ全体で 114 件の指摘を出して失敗し続けている。
現状では prek を実行すると、自動修正できる 69 ファイル分の差分が出るたびに `ruff check` だけが Failed で終わり、prek が通らない。
指摘のうち 3 件は `F821` (未定義名) で、実行時に `NameError` になりうる実際の不具合である。

pre-commit フックとして常に失敗する状態を放置すると、書き手が「Failed は想定内」とみなすようになり、新しく混入した指摘も見落とされる。
指摘をゼロにして、prek を実行するたびに全てのフックが Passed になる状態に戻す。

## 現状

### 再現手順

クリーンな `develop` ブランチで `prek run --all-files` を実行する。

```text
trim trailing whitespace.................................................Passed  (修正あり)
ruff format..............................................................Passed
...
clang-format.............................................................Passed
ruff check...............................................................Failed
  Found 114 errors.
  [*] 41 fixable with the `--fix` option (1 hidden fix can be enabled with the `--unsafe-fixes` option).
```

修正系フックが終わった後の 2 回目の実行でも `ruff check` は同じ 114 件で Failed になる。
2026-11-16 に `develop` の `05bd7c0a` で確認した。

### 指摘の内訳

`ruff 0.16.0` で `--statistics` を取った結果は次のとおり。

| ルール | 件数 | 自動修正 | 対象ファイル |
| --- | --- | --- | --- |
| BLE001 (`blind-except`) | 20 | 不可 | `e2e-test/sumomo_debug_windows.py` 16 件、`e2e-test/sumomo.py` 4 件 |
| UP045 (`non-pep604-annotation-optional`) | 12 | 可 | `run.py` 6 件、`examples/*/run.py` 6 件 |
| SIM117 (`multiple-with-statements`) | 11 | 一部 | `e2e-test/test_sumomo_*.py` |
| UP006 (`non-pep585-annotation`) | 10 | 可 | `run.py` 4 件、`examples/*/run.py` 6 件 |
| LOG015 (`root-logger-call`) | 8 | 不可 | `run.py` |
| PLW1510 (`subprocess-run-without-check`) | 8 | 不可 | `canary.py` 5 件、`e2e-test/sumomo.py` 1 件、`e2e-test/sumomo_debug_windows.py` 1 件、`e2e-test/test_sumomo_device.py` 1 件 |
| S110 (`try-except-pass`) | 8 | 不可 | `e2e-test/sumomo_debug_windows.py` 5 件、`e2e-test/sumomo.py` 3 件 |
| TRY002 (`raise-vanilla-class`) | 6 | 不可 | `run.py` |
| UP035 (`deprecated-import`) | 6 | 一部 | `run.py` 2 件、`examples/*/run.py` 3 件、`e2e-test/sumomo_debug_windows.py` 1 件 |
| SIM114 (`if-with-same-arms`) | 5 | 可 | `examples/*/run.py` |
| I001 (`unsorted-imports`) | 4 | 可 | `e2e-test/test_sumomo_tls_verification.py`、`examples/*/run.py` |
| SIM102 (`collapsible-if`) | 4 | 一部 | `run.py` 2 件、`e2e-test/sumomo.py` 2 件 |
| F821 (`undefined-name`) | 3 | 不可 | `e2e-test/sumomo_debug_windows.py` 1 件、`e2e-test/test_sumomo_basic.py` 2 件 |
| RUF100 (`unused-noqa`) | 3 | 可 | `examples/*/run.py` |
| SIM115 (`open-file-with-context-handler`) | 2 | 不可 | `run.py` |
| E722 (`bare-except`) | 1 | 不可 | `e2e-test/sumomo.py` |
| F401 (`unused-import`) | 1 | 可 | `e2e-test/sumomo_debug_windows.py` |
| PLW0120 (`useless-else-on-loop`) | 1 | 可 | `run.py` |
| UP037 (`quoted-annotation`) | 1 | 可 | `e2e-test/sumomo_debug_windows.py` |

### F821 の 3 件は実際の不具合

- `e2e-test/sumomo_debug_windows.py` のクラス `SumomoDebugWindows` の `__enter__` が `-> "SumomoWindows"` と宣言しており、存在しない名前を参照している。`from __future__ import annotations` があるためモジュール読み込み自体は通るが、`typing.get_type_hints()` など注釈の評価が起きる経路では `NameError` になる。
- `e2e-test/test_sumomo_basic.py` のテスト関数内で `list[dict[str, Any]]` と注釈しているが、`Any` を import していない。

### ルールが増えた経緯

- `prek.toml` の ruff は以前 `localhost` フックで `ruff check --fix` / `ruff format` を `language = "system"` で呼んでいた。この段階では PATH 上の ruff の既定ルール (`E4` / `E7` / `E9` / `F`) だけが適用されていた。
- `9b8f4e29` で ruff を `https://github.com/astral-sh/ruff-pre-commit` の `rev = "v0.16.0"` に切り替えた。`ruff-pre-commit` の `ruff-check` は既定で `--fix` に加えて `--select E4,E7,E9,F` を付けない代わりに、`extend-select = ["E4", "E7", "E9", "F"]` 相当の拡張ルールセット (`BLE` / `SIM` / `UP` / `TRY` / `S` / `PLW` / `LOG` / `I` / `RUF` など) を選択する。これにより既存コードが持っていた指摘が一斉に可視化された。
- ルートの `ruff.toml` は `line-length = 100` のみで、ルール選択・除外の設定を持っていない。`e2e-test/pyproject.toml` の `[tool.ruff]` も `line-length = 100` のみである。`e2e-test` 配下では最も近い設定ファイルである `e2e-test/pyproject.toml` が使われるため、ruff の既定ルールセットが適用される。

### CI の扱い

`.github/workflows/formatter.yml` は `python3 run.py iwyu` と `python3 run.py format` を実行したうえで `git diff --exit-code` を確認するだけで、prek も ruff も実行していない。
一方 `issues/closed/0106-bug-fix-windows-e2e-port-allocation.md` は完了条件に `uv run --directory=e2e-test ruff check` と `ruff format --check` が緑であることを挙げており、PR ごとに ruff check を通していない現状では、この条件を継続的に担保できていない。

## 設計方針

- 114 件の指摘をゼロにし、2 回目の `prek run --all-files` で `ruff check` が Passed になる状態にする
- 自動修正 (41 件) は `ruff check --fix` で適用し、残りは手で修正する
- 各ルールについて、コードを修正するかルールを除外するかを個別に判断する。判断基準は次のとおり
  - `F821` / `F401` / `E722` / `RUF100` はコード側を修正する
  - `UP*` / `SIM*` / `I001` / `PLW0120` などは挙動を変えない機械的な書き換えで済むため、コード側を修正する
  - `BLE001` / `S110` / `PLW1510` / `LOG015` / `TRY002` は、`run.py` や E2E ヘルパーの「失敗しても処理を続ける」設計やルートロガーによるログ出力を意図的に行っている箇所を含む。修正で挙動が変わる場合はコードを変えず、`prek.toml` の `ruff-check` フックの `args` かルートの `ruff.toml` で該当ルールを除外し、除外理由をコメントで残す
- ルールの除外は最小限にとどめ、除外したルールが増えた場合でも理由がわかるようにする
- 除外を決めた場合は、`e2e-test` 配下も含めて同じ方針になるよう、ルートの `ruff.toml` と `e2e-test/pyproject.toml` の `[tool.ruff]` の整合を取る
- ruff のバージョンは `prek.toml` の `rev` (`v0.16.0`) と `e2e-test/pyproject.toml` の `ruff~=0.16.0` を据え置き、バージョン変更による指摘の変化と本修正を混在させない
- モックやスタブは利用しない

## テスト戦略

- 修正前の状態で `prek run --all-files` が `ruff check` の 114 件で失敗することを確認する
- 修正後、2 回連続で `prek run --all-files` を実行し、全てのフックが Passed で差分が増えないことを確認する
- `run.py` / `canary.py` / `examples/*/run.py` を変更した場合は、`python3 run.py --help` と各 `examples/*/run.py --help`、`python3 canary.py --help` が実行でき、エラーにならないことを確認する
- `e2e-test` 配下を変更した場合は、`uv run --directory=e2e-test ruff check` と `uv run --directory=e2e-test ruff format --check` が緑であることを確認する
- `e2e-test/sumomo_debug_windows.py` を変更した場合は、`python3 -c "import sumomo_debug_windows"` 相当の import 確認と、`SumomoDebugWindows` の型注釈が評価できること (`typing.get_type_hints()` で `__enter__` を解決できること) を確認する
- 影響する E2E テストを 1 ケース以上実行して、ヘルパーの挙動に回帰がないことを確認する。Sora サーバーが利用できる環境では `AMD_AMF=1 uv run --directory=e2e-test pytest test_sumomo_amd_amf.py::test_sendonly_recvonly[H264] -v -s --timeout=60` のように個別に実行し、全テストは実行しない
- モックやスタブを利用していないことを確認する

## 完了条件

- 2 回目の `prek run --all-files` で `ruff check` を含む全てのフックが Passed になる
- `run.py` / `canary.py` / `examples/*/run.py` / `e2e-test/*.py` の 114 件の指摘が、コード修正または理由を明記したルール除外でなくなっている
- `e2e-test/sumomo_debug_windows.py` の `SumomoDebugWindows` を参照する未定義名と、`e2e-test/test_sumomo_basic.py` の `Any` 未 import が修正されている
- ルールを除外した場合は、除外したルールと除外理由が設定ファイルにコメントで残っている
- `python3 run.py --help`、`python3 canary.py --help`、各 `examples/*/run.py --help` が成功する
- E2E ヘルパーの挙動に回帰がないことを個別のテストケースで確認している
- モックやスタブを利用していない

## 解決方法

{どのように対応するのかを明確にすること}
