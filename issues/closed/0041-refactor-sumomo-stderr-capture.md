# sumomo.py の stderr キャプチャ設計を改善する

- Priority: Medium
- Created: 2026-07-20
- Completed: 2026-07-21
- Model: qwen3.8-max-preview
- Branch: feature/refactor-sumomo-stderr-capture
- Polished: 2026-07-20

## 目的

`e2e-test/sumomo.py` の `_capture_stderr_on_exit` / `_get_stderr_output` / `_cleanup` に存在する未使用引数・二重 join・重複ロジックを解消し、stderr キャプチャの設計を簡潔にする。

## 優先度根拠

機能上のバグではないが、未使用引数が呼び出し側に「この引数が何かに使われている」という誤った期待を与え、同一ロジック（join + `"".join(self._stderr_lines)`）が 3 箇所に分散していることで保守性が低下している。テスト基盤の可読性・保守性に直結するため Medium。

## 現状

`_wait_for_startup` のエラーパス 3 箇所（766, 800, 834 行目）で以下の呼び出しパターンが使われている:

```python
self._capture_stderr_on_exit(error_msg)
stderr_output = self._get_stderr_output()
```

問題点:

1. `_capture_stderr_on_exit(self, error_msg: str)` の `error_msg` 引数が本体で一切使われていない（`e2e-test/sumomo.py:853`）
2. `_capture_stderr_on_exit` が `self._stderr_reader.join(timeout=5)` + `self.stderr_output` 設定を行い、直後に `_get_stderr_output` が再度 `join(timeout=5)` + `"".join(self._stderr_lines)` を実行する（`e2e-test/sumomo.py:857-864`）。スレッド終了済みの join は即座に返るためレイテンシの問題ではないが、同一ロジックの二重実行は可読性を損なう
3. `_cleanup`（`e2e-test/sumomo.py:892-894`）でも 3 回目の join + `self.stderr_output` 再設定が行われる
4. `_capture_stderr_on_exit` が設定した `self.stderr_output` を `_get_stderr_output` が使わず、`self._stderr_lines` から再構築する
5. `_get_stderr_output` のフォールバックパス（`e2e-test/sumomo.py:865-869`、`self.process.stderr.read()`）はデッドコードである。`capture_stderr=False` 時は `stderr=None` で `Popen` される（`sumomo.py:482`）ため `self.process.stderr` は `None` となり到達しない。`capture_stderr=True` 時は最初の `if`（862 行目）で捕捉される

## 設計方針

`_capture_stderr_on_exit` を廃止し、`_get_stderr_output` に一本化する:

- `_get_stderr_output` 内で `join(timeout=5)` + `self.stderr_output` 設定 + 返却を行う
- `_get_stderr_output` のフォールバックパス（865-869 行目）はデッドコードのため削除する
- `_cleanup` 内の join + 設定ロジック（892-894 行目）を `_get_stderr_output` 呼び出しに置き換える。呼び出し位置は `stderr.close()`（897 行目）より前とする（stderr 内容を確定させてからリソースを解放するため）
- `error_msg` 引数は削除する（呼び出し側で `error_msg` への追記は `_get_stderr_output` の返却値で行う既存パターンを維持）
- `_wait_for_startup` の全エラーパス（766, 800, 834 行目）で `_get_stderr_output` 呼び出し後に `_cleanup` が呼ばれるため、同一フローで `_get_stderr_output` が 2 回呼ばれる（766, 800 行目は `__enter__` の例外ハンドラ（528-531 行目）経由、834 行目は直接呼び出し。`__enter__` が例外を送出した場合 `__exit__` は呼ばれない）。`_get_stderr_output` は冪等（join はスレッド終了済みなら即座に返る）のため問題ない
- スレッド安全性の不変条件: `_get_stderr_output` の呼び出し時点でプロセスは必ず終了している（`poll() != None` の確認後、または `terminate` + `wait` の後）。パイプの書き込み側が閉じているためリーダースレッドは即座に終了し、`join(timeout=5)` がタイムアウトすることは実質的にない
- `_cleanup` 内の `stderr.close()`（897-901 行目）は `_get_stderr_output` 呼び出しの後に残す（リソース解放は `_cleanup` の責務）。`_read_stderr`（554 行目）の `finally` でも `stderr.close()` が呼ばれるため二重クローズになるが、既存の `try/except` で吸収済みであり防御的クローズとして意図的に残す
- 呼び出し側は `_get_stderr_output` の返り値をエラーメッセージ構築に使用する。`self.stderr_output` への設定は副次効果であり、テストコード（`test_sumomo_tls_verification.py:108,135,162,195,227`、`test_sumomo_device.py:188,300,402`）が `with` ブロック終了後に `sumomo.stderr_output` を参照する契約を支えている。`_cleanup` 内の呼び出しは返り値未使用だが `self.stderr_output` の設定のために必要であり、削除してはいけない
- `_get_stderr_output` の戻り値型は `str | None` のまま維持する（`capture_stderr=False` 時は `None`、`True` 時は `str`）

## 解決方法

`_capture_stderr_on_exit` を削除し、`_get_stderr_output` に一本化した。`_get_stderr_output` 内で `self.stderr_output` への代入も行うようにした。`_cleanup` 内の重複ロジックは `_get_stderr_output` 呼び出しに置き換えた。`_wait_for_startup` 内の 3 箇所の `_capture_stderr_on_exit` 呼び出しは削除した。

## 完了条件

- `_capture_stderr_on_exit` メソッドが削除されている
- `_get_stderr_output` が join + `self.stderr_output` 設定 + 返却を一手に担う
- `_get_stderr_output` のデッドコード（フォールバックパス）が削除されている
- `_cleanup` 内の stderr 取得ロジックが `_get_stderr_output` 呼び出しに置き換わっている
- 既存の E2E テスト（`test_sumomo_basic.py`、`test_sumomo_tls_verification.py`、`test_sumomo_device.py`）が全通過する
- `uv run --directory=e2e-test ruff check` で lint エラーがない
