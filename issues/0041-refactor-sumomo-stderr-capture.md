# sumomo.py の stderr キャプチャ設計を改善する

- Priority: Medium
- Created: 2026-07-20
- Completed: {YYYY-MM-DD}
- Model: qwen3.8-max-preview
- Branch: feature/refactor-sumomo-stderr-capture
- Polished: {YYYY-MM-DD}

## 目的

`e2e-test/sumomo.py` の `_capture_stderr_on_exit` / `_get_stderr_output` / `_cleanup` に存在する未使用引数・二重 join・重複ロジックを解消し、stderr キャプチャの設計を簡潔にする。

## 優先度根拠

機能上のバグではないが、未使用引数が呼び出し側に誤った期待を与え、二重 join がエラーパスのレイテンシを増大させる。テスト基盤の可読性・保守性に直結するため Medium。

## 現状

`_wait_for_startup` のエラーパス 3 箇所（766, 800, 834 行目）で以下の呼び出しパターンが使われている:

```python
self._capture_stderr_on_exit(error_msg)
stderr_output = self._get_stderr_output()
```

問題点:

1. `_capture_stderr_on_exit(self, error_msg: str)` の `error_msg` 引数が本体で一切使われていない（`e2e-test/sumomo.py:853`）
2. `_capture_stderr_on_exit` が `self._stderr_reader.join(timeout=5)` + `self.stderr_output` 設定を行い、直後に `_get_stderr_output` が再度 `join(timeout=5)` + `"".join(self._stderr_lines)` を実行する（`e2e-test/sumomo.py:857-864`）
3. `_cleanup`（`e2e-test/sumomo.py:892-894`）でも 3 回目の join + `self.stderr_output` 再設定が行われる
4. `_capture_stderr_on_exit` が設定した `self.stderr_output` を `_get_stderr_output` が使わず、`self._stderr_lines` から再構築する

## 設計方針

`_capture_stderr_on_exit` を廃止し、`_get_stderr_output` に一本化する:

- `_get_stderr_output` 内で `join(timeout=5)` + `self.stderr_output` 設定 + 返却を行う
- `_cleanup` 内の join + 設定ロジックも `_get_stderr_output` 呼び出しに置き換える
- `error_msg` 引数は削除する（呼び出し側で `error_msg` への追記は `_get_stderr_output` の返却値で行う既存パターンを維持）

## 完了条件

- `_capture_stderr_on_exit` メソッドが削除されている
- `_get_stderr_output` が join + `self.stderr_output` 設定 + 返却を一手に担う
- `_cleanup` 内の stderr 取得ロジックが `_get_stderr_output` 呼び出しに置き換わっている
- 既存の E2E テスト（`test_sumomo_basic.py`、`test_sumomo_tls_verification.py`）が全通過する
