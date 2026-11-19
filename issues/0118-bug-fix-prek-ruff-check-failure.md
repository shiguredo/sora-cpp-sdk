# prek run の ruff check がリポジトリ全体で 114 件失敗し、自動修正できない問題を解消する

- Created: 2026-11-16
- Completed: {YYYY-MM-DD}
- Branch: feature/fix-prek-ruff-check
- Polished: 2026-11-19

## 目的

`prek run --all-files` の最後に実行される `ruff check` が、リポジトリ全体で 114 件の指摘を出して失敗し続けている。
現状では prek を実行すると、自動修正の対象になる 41 件 (8 ファイル) の差分が出るたびに `ruff check` だけが Failed で終わり、prek が通らない。
指摘の中には `F821` (未定義名) が 3 件含まれる。

- `e2e-test/sumomo_debug_windows.py` のクラス `SumomoDebugWindows` の `__enter__` は、存在しない `SumomoWindows` を型注釈で参照している。`from __future__ import annotations` があるため import とメソッド呼び出しは通るが、`typing.get_type_hints()` など注釈を評価する経路では `NameError` になる。
- `e2e-test/test_sumomo_basic.py` のテスト関数は `Any` を import していない。注釈が関数本体内のローカル変数への注釈であるため実行時に評価されず、`NameError` にはならないが、型注釈として未定義名を参照したままである。
- `e2e-test/sumomo_debug_windows.py` のクラス `SumomoDebugWindows` は、リポジトリ内のどこからも参照されていない。このファイル単体を修正対象にする価値が低い。

pre-commit フックとして常に失敗する状態を放置すると、書き手が「Failed は想定内」とみなすようになり、新しく混入した指摘も見落とされる。
ruff の既定ルール (プレビューを除く 413 ルール) がかかり続ける現状では、新しく混入する指摘も同じように積み上がる。
適用するルールを明示したうえで、prek を実行するたびに全てのフックが Passed になる状態に戻す。

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
また `git ls-files '*.py'` の 19 ファイルを ruff 0.16.0 で直接検査しても同じ 114 件になる (フックと同じ条件)。

### 指摘の内訳

`ruff 0.16.0` で `--statistics` を取った結果は次のとおり。`自動修正` 列の `可` は safe fix を持つ指摘が全件あること、`一部 (n 件)` は一部だけが safe fix を持つことを示す。`--unsafe-fixes` を付けないと修正できないものは `不可` として扱う。

| ルール | 件数 | 自動修正 | 対象ファイル |
| --- | --- | --- | --- |
| BLE001 (`blind-except`) | 20 | 不可 | `e2e-test/sumomo_debug_windows.py` 16 件、`e2e-test/sumomo.py` 4 件 |
| UP045 (`non-pep604-annotation-optional`) | 12 | 可 | `run.py` 6 件、`examples/*/run.py` 6 件 |
| SIM117 (`multiple-with-statements`) | 11 | 一部 (3 件) | `e2e-test/test_sumomo_*.py` |
| UP006 (`non-pep585-annotation`) | 10 | 可 | `run.py` 4 件、`examples/*/run.py` 6 件 |
| LOG015 (`root-logger-call`) | 8 | 不可 | `run.py` |
| PLW1510 (`subprocess-run-without-check`) | 8 | 不可 | `canary.py` 5 件、`e2e-test/sumomo.py` 1 件、`e2e-test/sumomo_debug_windows.py` 1 件、`e2e-test/test_sumomo_device.py` 1 件 |
| S110 (`try-except-pass`) | 8 | 不可 | `e2e-test/sumomo_debug_windows.py` 5 件、`e2e-test/sumomo.py` 3 件 |
| TRY002 (`raise-vanilla-class`) | 6 | 不可 | `run.py` |
| UP035 (`deprecated-import`) | 6 | 一部 (1 件) | `run.py` 2 件、`examples/*/run.py` 3 件、`e2e-test/sumomo_debug_windows.py` 1 件 |
| SIM114 (`if-with-same-arms`) | 5 | 可 | `examples/*/run.py` |
| I001 (`unsorted-imports`) | 4 | 可 | `e2e-test/test_sumomo_tls_verification.py`、`examples/*/run.py` |
| SIM102 (`collapsible-if`) | 4 | 不可 (unsafe fix 1 件) | `run.py` 2 件、`e2e-test/sumomo.py` 2 件 |
| F821 (`undefined-name`) | 3 | 不可 | `e2e-test/sumomo_debug_windows.py` 1 件、`e2e-test/test_sumomo_basic.py` 2 件 |
| RUF100 (`unused-noqa`) | 3 | 可 | `examples/*/run.py` |
| SIM115 (`open-file-with-context-handler`) | 2 | 不可 | `run.py` |
| E722 (`bare-except`) | 1 | 不可 | `e2e-test/sumomo.py` |
| F401 (`unused-import`) | 1 | 可 | `e2e-test/sumomo_debug_windows.py` |
| PLW0120 (`useless-else-on-loop`) | 1 | 可 | `run.py` |
| UP037 (`quoted-annotation`) | 1 | 可 | `e2e-test/sumomo_debug_windows.py` |

`ruff check --fix` を 1 回適用した後の残存は 68 件になる。適用した時点で内容が変わるファイルは 8 件である (`run.py`、`examples/*/run.py` 3 件、`e2e-test/sumomo_debug_windows.py`、`e2e-test/test_sumomo_basic.py`、`e2e-test/test_sumomo_openh264.py`、`e2e-test/test_sumomo_tls_verification.py`)。
`canary.py` の 5 件は `--fix` では消えない。
`--fix` 実行時のサマリは `Found 119 errors (51 fixed, 68 remaining)` となり、修正前の 114 件とも `41 fixable` とも一致しない (SIM117 の修正で同じファイルに新たに生じる指摘を同じ実行内で計上するため)。判定に使う値は、適用後の `ruff check` が 68 件であることとする。

### F821 の 3 件

- `e2e-test/test_sumomo_basic.py` のテスト関数内で `list[dict[str, Any]]` と注釈しているが、`Any` を import していない。これは関数本体内のローカル変数への注釈であり、実行時には評価されないため `NameError` にはならない。ただし未定義名を型注釈で参照したままになり、型検査や注釈の静的解析で `F821` として検出され続ける。
- `e2e-test/sumomo_debug_windows.py` のクラス `SumomoDebugWindows` の `__enter__` が `-> "SumomoWindows"` と宣言している。`SumomoWindows` は定義されていない。`from __future__ import annotations` があるため import とメソッド呼び出しは通るが、`typing.get_type_hints()` など注釈を評価する経路では `NameError` になる。

### ルールが増えた経緯

- `prek.toml` の ruff は以前 `repo = "local"` のフックで `ruff check --fix` / `ruff format` を `language = "system"` で呼んでいた。このときのルール選択は ruff の既定値だった。
- `9b8f4e29` で ruff を `https://github.com/astral-sh/ruff-pre-commit` の `rev = "v0.16.0"` に切り替えた。`ruff-pre-commit` の `ruff-check` は `entry: ruff check --force-exclude`、`args: []` で、`--fix` もルール選択も指定しない。ルール選択はリポジトリ側の設定ファイルに委ねられている。
- ルートの `ruff.toml` は `line-length = 100` のみで、`[lint]` の `select` を持たない。そのため ruff の既定ルール (プレビューを除く 413 ルール) が適用される。既定ルールには `BLE` / `SIM` / `UP` / `TRY` / `S` / `PLW` / `LOG` / `I` / `RUF` だけでなく `B` (29) / `C4` (17) / `PT` (6) なども含まれる。
- 旧設定で想定していた `select = ["E4", "E7", "E9", "F"]` 相当で検査した場合、指摘は 5 件 (F821 3 件、E722 1 件、F401 1 件) になる。114 件のうち 109 件は、ルール選択が「E4 / E7 / E9 / F」から既定ルール全体へ広がったことで可視化されたものである。
- ruff 0.16.0 の既定ルールセット全体のうち、現在のコードで指摘が出ているのは上記 19 ルールだけである。`B` / `C4` / `PT` などの指摘は 0 件である。

### e2e-test の設定の解決

- ruff は対象ファイルに最も近い設定ファイルを使う。`e2e-test/pyproject.toml` の `[tool.ruff]` は `line-length = 100` のみで `[tool.ruff.lint]` を持たないため、`e2e-test` 配下のファイルにも ruff の既定ルールが適用される。
- `prek run --all-files` はリポジトリルートで `ruff check --force-exclude <ファイル>` を実行するが、`--force-exclude` は除外パターンを強制するだけで設定ファイルの探索方法は変えない。そのため `e2e-test` 配下も `e2e-test/pyproject.toml` (とその既定ルール) で検査される。
- 実測では、ルートから `git ls-files '*.py'` を渡した結果と、`e2e-test` 直下から `ruff check .` を実行した結果の件数内訳は一致した。

### 重複した指摘と参照されていないファイル

- `e2e-test/sumomo.py` の `except: pass` は、同じ行に対して E722 と S110 の両方が出ている。
- `e2e-test/sumomo_debug_windows.py` の `SumomoDebugWindows` は、リポジトリ内のどこからも参照されていない。E2E テストから使われるのは `e2e-test/sumomo.py` の `Sumomo` である。

### スコープ外

- 本 issue では CI (`.github/workflows/formatter.yml` / `ci.yml`) を変更しない。CI に prek (ruff) を組み込む作業は別 issue で扱う。
- 本 issue では ruff のバージョン (`prek.toml` の `rev` と `e2e-test/pyproject.toml` の `ruff~=0.16.0`) を変更しない。
- `shiguredo-python` スキルは ruff / ty を `pyproject.toml` の依存に含めずフック経由で実行することを定めているが、`e2e-test/pyproject.toml` は ruff / ty を `dev` 依存に持つ。この規約との整合 (ruff / ty の依存からの削除) は別 issue で扱う。
- `ty` / `tombi` フックの追加 (スキルで定める最低限のフック構成) は別 issue で扱う。

## 設計方針

- 114 件の指摘をゼロにし、`prek run --all-files` で `ruff check` が Passed になる状態にする。
- ルール選択は `[lint]` の `ignore` ではなく `select` で明示的に決める。`select` を書かない限り ruff の既定ルール (413 ルール) が適用され、ruff の更新で既定が変われば prek が再び FAIL する構造が残るため。
- `select` の候補は「旧設定が想定していた `E4` / `E7` / `E9` / `F`」に、現在実際に出ている指摘のルール (`I` / `RUF` / `SIM` / `UP` / `PLW` から `PLW0120` と `PLW1510` / `LOG` / `BLE` / `S` から `S110` / `TRY` から `TRY002` / `E722`) を加えたものとする。`B` / `C4` / `PT` など、現在指摘が 0 件のルールは含めない。
- ルール選択の正本はルートの `ruff.toml` のみとする。`prek.toml` の `ruff-check` の `args` は使わない (`uv run --directory=e2e-test ruff check` と prek のフックで同じ結論にならなくなるため)。`e2e-test/pyproject.toml` には `[tool.ruff]` の `extend = "../ruff.toml"` を追加してルートの `select` を継承させ、`line-length = 100` の二重管理を解消する。
- `select` に含めたルールは、コード修正で解消することを基本とする。コード修正で解消できないものは `select` から外し、外した理由を `ruff.toml` のコメントに残す。
- 個別の扱いは次のとおり。
  - `F821` / `F401` / `E722` / `RUF100` / `I001` はコードを修正する。`E722` を `except Exception` に変えることで、同じ行の `S110` も同時に解消される。
  - `UP045` / `UP006` / `UP035` / `UP037` / `PLW0120` / `SIM114` / `SIM102` / `SIM117` / `SIM115` は、挙動を変えない書き換えで修正する。
  - `LOG015` は `logging.basicConfig` でルートロガーに設定したうえでモジュールレベルの `logging.info` / `logging.error` / `logging.warning` を呼んでいる。`logging.getLogger(__name__)` でロガーを取得して呼ぶ形に変え、ルートロガーへの直接呼び出しをやめる。
  - `TRY002` は `run.py` の `raise Exception(...)` を、`RuntimeError` など用途に合う組み込み例外に置き換える。
  - `PLW1510` は `subprocess.run` の結果を明示的に取り出して `returncode` を確認する形に変える。`check=True` にすると失敗時の挙動が変わるため、呼び出し元の意図を確認して `check=True` か `returncode` の明示的な確認かを選ぶ。
  - `BLE001` / `S110` は、`except Exception` で捕まえて元の例外を再 raise するか、`except` を具体的な例外型に絞るか、ログ出力を伴う形にする。`e2e-test/sumomo_debug_windows.py` は参照されていないため、ファイルごと削除するか、削除せずにコードを修正するかを実装時に決め、決めた理由を PR 本文に残す。ファイルを削除する場合は `F821` / `F401` / `UP035` / `UP037` / `S110` / `PLW1510` / `BLE001` の 26 件が同時に消える。
- ruff のバージョンは `prek.toml` の `rev` (`v0.16.0`) と `e2e-test/pyproject.toml` の `ruff~=0.16.0` を据え置き、バージョン変更による指摘の変化と本修正を混在させない。
- モックやスタブは利用しない。

## テスト戦略

- 修正前の状態で `prek run --all-files` が `ruff check` の 114 件で失敗することを確認する。
- 修正後、`ruff check --statistics .` の出力が `Found 0 errors` になることを確認する。
- `prek run --all-files` を 2 回連続で実行し、全てのフックが Passed で差分が増えないことを確認する。
- `prek run` は修正系フックがファイルを書き換えるため、実行前に `git status` を確認し、対象ファイルをリポジトリ外へ退避しておく。
- `run.py` / `canary.py` / `examples/*/run.py` を変更した場合は、`python3 run.py --help`、`python3 canary.py --help`、`python3 examples/<sample>/run.py --help` が終了コード 0 で終わることを確認する。
- `e2e-test/sumomo.py` を変更した場合は、`uv run --directory=e2e-test python3 -c "import sumomo"` で import できることを確認する。`uv` が使えない環境では、事前に `e2e-test/.venv` を作らずに済むよう `uv sync --directory=e2e-test` を実行してから同じコマンドを実行する。
- `e2e-test/sumomo_debug_windows.py` を削除しない場合は、`uv run --directory=e2e-test python3 -c "import sumomo_debug_windows, typing; typing.get_type_hints(sumomo_debug_windows.SumomoDebugWindows.__enter__)"` が成功することを確認する (`httpx` が必要なため `uv run` 経由で実行する)。
- `e2e-test/test_sumomo_basic.py` の `Any` 未 import は、関数本体内のローカル変数への注釈であるため実行時に評価されず、テストの実行では検出できない。修正の確認は `ruff check` の 0 件 (F821 の解消) で行う。E2E テストは `shiguredo-test` スキルの規約に従い GitHub Actions でのみ実行するため、ローカルでは実行せず、回帰が無いことは PR の CI で当該テストが成功することで確認する。
- `e2e-test` 配下の指摘をルール除外で解消した場合、`uv run --directory=e2e-test ruff check` が `prek run --all-files` と同じ結論 (0 件) になることを確認する。
- モックやスタブを利用していないことを確認する。

## 完了条件

- `ruff check --statistics .` が `Found 0 errors` になる。
- 2 回目の `prek run --all-files` で `ruff check` を含む全てのフックが Passed になる。
- ルートの `ruff.toml` の `[lint].select` でルールが明示され、ruff の既定ルールに依存しない。
- `e2e-test/pyproject.toml` の `[tool.ruff]` が `extend = "../ruff.toml"` でルート設定を継承し、`uv run --directory=e2e-test ruff check` と `prek run --all-files` の `ruff check` が同じ結論になる。
- `select` から外したルールは、除外理由が `ruff.toml` のコメントに残っている。
- `e2e-test/test_sumomo_basic.py` の `Any` 未 import が修正され、`ruff check` の 0 件で `F821` が解消している。あわせて PR の CI で当該テストが成功する (注釈の未定義名は実行時の `NameError` を起こさないため、CI の成功は回帰が無いことの確認である)。
- `python3 run.py --help`、`python3 canary.py --help`、各 `examples/*/run.py --help` が終了コード 0 で終わる。
- モックやスタブを利用していない。

## 解決方法

{どのように対応するのかを明確にすること}
