# e2e-test の sumomo.py の Windows 用 crashed 判定を改善する

- Priority: Low
- Created: 2026-06-11
- Polished: {YYYY-MM-DD}
- Model: Opus 4.7
- Branch: feature/refactor-sumomo-py-windows-exit-detection

## 目的

`e2e-test/sumomo.py:809-812` および `e2e-test/sumomo.py:820-826` は Windows 上で `get_stats()` 呼び出し時にプロセスが既に終了していると、exit code に関係なく `RuntimeError("sumomo.exe has crashed ...")` を投げる。Sora 側からの切断 (`wscode=4490`) で sumomo が disconnect ハンドラに従って正常終了 (`exit code 0`) した場合まで "crashed" と表現されてしまい、本物のクラッシュ (segfault や abort) との切り分けが困難になっている。exit code に応じて表現を変える。

## 優先度根拠

挙動自体は正しい (= プロセスが消えていれば `get_stats` は失敗する) ためテストの合否判定には影響しない。あくまでログとエラーメッセージの読みやすさの問題なので Low。

## 現状

`e2e-test/sumomo.py:809-812`:

```python
# Windows の場合、プロセスが生きているか確認
if self.process and platform.system().lower() == "windows":
    if self.process.poll() is not None:
        raise RuntimeError(f"sumomo.exe has crashed (exit code: {self.process.returncode})")
```

`e2e-test/sumomo.py:820-826`:

```python
except Exception as e:
    # Windows の場合、エラー時にプロセスの状態を確認
    if self.process and platform.system().lower() == "windows":
        if self.process.poll() is not None:
            raise RuntimeError(
                f"sumomo.exe crashed while getting stats (exit code: {self.process.returncode})\n"
                f"Original error: {e}"
            )
    raise
```

どちらも `returncode` の値に関係なく "crashed" 表現を使っている。0004 の 2026-06-11 windows ジョブのログでは `exit code: 0` で "has crashed" と表示されており、実際には Sora 切断による正常終了であった。

## 設計方針

`returncode` を見て表現を分岐する:

- `returncode == 0` のとき: 正常終了。"sumomo.exe exited unexpectedly (probably disconnected by server)" のような表現にする
- `returncode != 0` のとき: 本物のクラッシュの可能性。従来通り "sumomo.exe has crashed (exit code: N)" を維持する

except 節の "crashed while getting stats" 表現も同様に分岐する。

ロジックの分岐ポイントが 2 箇所に発生するため、判定を小さなヘルパー関数 (`_format_unexpected_exit_error(returncode, original_error=None)` のような形) として `Sumomo` クラス内に切り出して重複を避けることを検討する。

## 完了条件

- `e2e-test/sumomo.py:809-812` および `e2e-test/sumomo.py:820-826` の RuntimeError メッセージが `returncode` で分岐されている
- `returncode == 0` のときは "crashed" という表現を使わない
- 既存テストが全て通る (テストの合否判定ロジックは不変、メッセージ表現のみの変更)
- `CHANGES.md` の `## develop` に `[CHANGE]` エントリを追記する

## 解決方法

該当 2 箇所を以下のように修正する:

```python
if self.process and platform.system().lower() == "windows":
    if self.process.poll() is not None:
        returncode = self.process.returncode
        if returncode == 0:
            raise RuntimeError(
                f"sumomo.exe exited unexpectedly (probably disconnected by server, exit code: {returncode})"
            )
        raise RuntimeError(f"sumomo.exe has crashed (exit code: {returncode})")
```

except 節も同様に分岐する。共通処理をヘルパー関数に切り出す場合は `Sumomo` クラス内のプライベートメソッドとして実装する。

`CHANGES.md` の `## develop` に `[CHANGE]` エントリを追記する (誤解を招くエラー表現を区別する変更)。
