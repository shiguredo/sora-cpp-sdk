# e2e-test の sumomo.py の Windows 用 crashed 判定を改善する

- Priority: Low
- Created: 2026-06-11
- Completed: {YYYY-MM-DD}
- Model: Opus 4.7
- Branch: feature/fix-sumomo-py-windows-exit-detection
- Polished: 2026-09-14
## 目的

`e2e-test/sumomo.py` の `Sumomo.get_stats()` 内の 2 箇所 (冒頭のプロセス生存確認と except 節) は Windows 上で、`get_stats()` 呼び出し時にプロセスが既に終了していると exit code に関係なく `RuntimeError("sumomo.exe has crashed ...")` を投げる。Sora 側からの WS 切断 (`wscode=4490`) や WS の graceful close で sumomo が正常終了するケース (exit code 0) でも "has crashed" と表示されてしまい、本物のクラッシュ (segfault や abort) との切り分けが困難になっている。exit code に応じて表現を分岐し、`Sumomo.__enter__` の Windows 早期終了チェックで既に使われている `exited unexpectedly with code N` の語彙と統一する。

## 優先度根拠

テスト合否ロジックは変えずエラーメッセージ表現を整えるだけのため Low。フレーキー発生時に「本物のクラッシュかどうか」を素早く判別できるようにする予防的改善。

## 現状

`e2e-test/sumomo.py` の `Sumomo.get_stats()` 冒頭 (`# Windows の場合、プロセスが生きているか確認` ブロック):

```python
# Windows の場合、プロセスが生きているか確認
if self.process and platform.system().lower() == "windows":
    if self.process.poll() is not None:
        raise RuntimeError(f"sumomo.exe has crashed (exit code: {self.process.returncode})")
```

`Sumomo.get_stats()` の except 節は同じパターンを使い、`crashed while getting stats (exit code: N)\nOriginal error: e` の形式で `RuntimeError` を投げる。

どちらも `returncode` の値に関係なく "crashed" 表現を使っている。2026-06-11 の windows schedule CI ジョブ (run 27318676246) では `exit code: 0` で "has crashed" と表示されたが、同じログに `wscode=4490 wsreason=INTERNAL-ERROR` が出ており、本物のクラッシュではなく Sora 側からの WS close 受信後の正常終了であったと推定される。

なお `Sumomo` クラス内で `exited unexpectedly with code N` の語彙を使うメッセージは `Sumomo.__enter__` の Windows 早期終了チェック (`sumomo.exe exited unexpectedly with code N`) と `Sumomo._wait_for_startup()` (`Process exited unexpectedly with code N`) の 2 箇所に存在する。

### 対象外

`e2e-test/sumomo_debug_windows.py` の `SumomoDebugWindows.get_stats()` 内の 2 箇所 (`sumomo.exe has crashed (exit code: N)` と `sumomo.exe crashed while getting stats (exit code: N)`) にも同じパターンが存在するが、当該ファイルは現状どこからも `import` されておらず Windows 調査用に残されているファイル。本 issue のスコープ外として手を入れない（必要なら別 issue で対応）。将来このファイルを再利用する際に同一の問題が再発する可能性は認識している。

### AGENTS.md 規約との関係

AGENTS.md は「テストのログメッセージは全て日本語にすること」と定めている。一方、Python 規約 (shiguredo-python) は「エラーメッセージは英語とすること」「テストのログメッセージは日本語とすること」と区別しており、`Sumomo` クラスが投げる `RuntimeError` はエラーメッセージに該当する。`Sumomo` クラスの既存 `RuntimeError` (`HTTP client not initialized`、`Process not started`、`sumomo.exe exited unexpectedly with code N` など) は全て英語で書かれており、本 issue でもこの方針を踏襲して英語のまま差し替える。なお `e2e-test/sumomo.py` の `get_device_lists()` の `--list-devices が異常終了した` は日本語であるが、これは `test_sumomo_device.py` の assert と同一文言を持つテスト向けヘルパーの例外であり、`Sumomo` クラスの例外とは性格が異なるため本 issue の判断の根拠とはしない。`e2e-test/` 配下メッセージ全体の言語方針の整理は別 issue で扱う余地があるが、本 issue のスコープ外とする。

## 設計方針

`returncode` を見て分岐し、`exit code 0` を本物のクラッシュと区別する。

- `returncode == 0` のとき: `sumomo.exe exited unexpectedly with code 0 (likely Sora WS close or network issue; check sumomo logs for wscode)` という中立的表現にする。`likely` を使い「サーバ切断と決め打ちしない」中立メッセージとする（内部例外や SIGTERM 等の可能性もあるため）
- `returncode != 0` のとき: 従来通り `sumomo.exe has crashed (exit code: N)` を維持する。`returncode` は Windows ランタイムで NTSTATUS 由来の大きな正の数 (例: STATUS_ACCESS_VIOLATION = 0xC0000005 = 3221225477。`_winapi.GetExitCodeProcess` は符号なし DWORD を返し、CPython は `PyLong_FromUnsignedLong` で Python int 化するため正の数になる) になる場合があるが、本 issue では既存の 10 進そのまま表示を踏襲する（表示形式変更は別 issue）

except 節も同じ分岐ルールで書き換え、元例外を `raise ... from e` でチェーンさせる。これに伴い現行コードの `\nOriginal error: {e}` の手動埋め込みは廃止し、Python 標準の chained traceback (`__cause__`) に任せる。この変更によりトレースバック出力に `The above exception was the direct cause of the following exception:` が追加される点は意図した動作とする。

ヘルパー関数化はしない（分岐ポイントは 2 箇所のみで、インライン記述で 5 行程度に収まり可読性が高いため。YAGNI を優先）。

## 完了条件

- `Sumomo.get_stats()` の冒頭の事前チェックと except 節の `RuntimeError` メッセージが `returncode` で分岐され、`returncode == 0` のとき "crashed" の語を含まない
- except 節では元例外を `raise ... from e` でチェーンしている（`\nOriginal error: {e}` の手動埋め込みは削除する）
- `uv run --directory=e2e-test ruff check sumomo.py` が緑である（`ty` は本 issue で新規導入しない、ruff のみで型表現以外の構文確認は足りる）
- `grep -cE "has crashed|crashed while" e2e-test/sumomo.py` の結果が `returncode != 0` の分岐内のみに出現し、exit code 0 の分岐には "crashed" が含まれていないことを目視確認する
- 本 issue の効果検証は schedule CI のフレーキー再発時にしか実機で観測できない（exit code 0 経路は意図的に再現困難）。PR マージ前は上の lint と grep の静的確認に絞り、実機検証は次回フレーキー再発時に「has crashed (exit code: 0)」が出ていないことを確認する
- `CHANGES.md` の `## develop` 配下、`### misc` セクションに以下の形式で `[FIX]` エントリを追記する。`### misc` セクション内では凡例順（CHANGE → ADD → UPDATE → FIX）を尊重し、既存の `[FIX]` エントリがあれば先頭の `[FIX]` の直後、無ければ末尾（最後の `[UPDATE]` の直後）に挿入する（現状 `### misc` に `[FIX]` エントリは存在しない）。`### misc` 内既存エントリの並べ替えは本 issue のスコープ外。担当者ハンドル `@<担当者>` は PR 作成者のものに書き換える:

  ```
  - [FIX] e2e-test/sumomo.py の Windows 用 crashed 判定を改善する
    - get_stats() で exit code 0 でもプロセス終了時に "has crashed" と表示されていたのを修正する
    - exit code 0 のときは "exited unexpectedly with code 0 (likely Sora WS close or network issue; check sumomo logs for wscode)"、非 0 のときは従来の "has crashed (exit code: N)" を出すように分岐する
    - @<担当者>
  ```

## 解決方法

`Sumomo.get_stats()` の冒頭 (`# Windows の場合、プロセスが生きているか確認` ブロック) を以下に差し替える:

```python
# Windows の場合、プロセスが生きているか確認
if self.process and platform.system().lower() == "windows":
    if self.process.poll() is not None:
        returncode = self.process.returncode
        if returncode == 0:
            raise RuntimeError(
                f"sumomo.exe exited unexpectedly with code 0 "
                f"(likely Sora WS close or network issue; check sumomo logs for wscode)"
            )
        raise RuntimeError(f"sumomo.exe has crashed (exit code: {returncode})")
```

`Sumomo.get_stats()` の except 節 (`# Windows の場合、エラー時にプロセスの状態を確認` ブロック) を以下に差し替える（元例外は `raise ... from e` でチェーンさせる）:

```python
except Exception as e:
    # Windows の場合、エラー時にプロセスの状態を確認
    if self.process and platform.system().lower() == "windows":
        if self.process.poll() is not None:
            returncode = self.process.returncode
            if returncode == 0:
                raise RuntimeError(
                    f"sumomo.exe exited unexpectedly with code 0 while getting stats "
                    f"(likely Sora WS close or network issue; check sumomo logs for wscode)"
                ) from e
            raise RuntimeError(
                f"sumomo.exe has crashed while getting stats (exit code: {returncode})"
            ) from e
    raise
```
