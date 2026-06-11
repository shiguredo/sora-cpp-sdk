# e2e-test の sumomo.py の Windows 用 crashed 判定を改善する

- Priority: Low
- Created: 2026-06-11
- Polished: 2026-06-11
- Model: Opus 4.7
- Branch: feature/refactor-sumomo-py-windows-exit-detection

## 目的

`e2e-test/sumomo.py:809-812` および `e2e-test/sumomo.py:820-826` は Windows 上で `get_stats()` 呼び出し時にプロセスが既に終了していると exit code に関係なく `RuntimeError("sumomo.exe has crashed ...")` を投げる。Sora 側からの WS 切断 (`wscode=4490`) や WS の graceful close で sumomo が disconnect ハンドラに従って正常終了するケース (exit code 0) でも "has crashed" と表示されてしまい、本物のクラッシュ (segfault や abort) との切り分けが困難になっている。exit code に応じて表現を分岐し、`sumomo.py:425, 660` で既に使われている `exited unexpectedly with code N` の語彙と統一する。

## 優先度根拠

テスト合否ロジックは変えずエラーメッセージ表現を整えるだけのため Low。0004 のフレーキー切り分けで「本物のクラッシュかどうか」を素早く判別できるようにしたい程度の改善。

## 現状

`e2e-test/sumomo.py:809-812`:

```python
# Windows の場合、プロセスが生きているか確認
if self.process and platform.system().lower() == "windows":
    if self.process.poll() is not None:
        raise RuntimeError(f"sumomo.exe has crashed (exit code: {self.process.returncode})")
```

`e2e-test/sumomo.py:820-826` は except 節で同じパターンを使い、`crashed while getting stats (exit code: N)\nOriginal error: e` の形式で `RuntimeError` を投げる。

どちらも `returncode` の値に関係なく "crashed" 表現を使っている。0004 の 2026-06-11 windows ジョブ (run 27318676246) では `exit code: 0` で "has crashed" と表示されたが、同じログに `wscode=4490 wsreason=INTERNAL-ERROR` が出ており、本物のクラッシュではなく Sora 側からの WS close 受信後の正常終了であったと推定される。

なお `e2e-test/sumomo.py` 内の他のメッセージは `sumomo.py:425` `sumomo.exe exited unexpectedly with code N` と `sumomo.py:660` `Process exited unexpectedly with code N` の 2 種類が存在する。本 issue では `get_stats()` 対象が sumomo プロセスであることに揃え、`sumomo.py:425` の `sumomo.exe exited unexpectedly with code N` 表現を採用する。

### 対象外

`e2e-test/sumomo_debug_windows.py:810, 865` にも同じ "has crashed" / "crashed while getting stats" パターンが存在するが、当該ファイルは現状どこからも `import` されておらず Windows 調査用に残されているファイル。本 issue のスコープ外として手を入れない (必要なら別 issue で対応)。

### AGENTS.md 規約との関係

AGENTS.md は「テストのログメッセージは全て日本語にする」と定めているが、`e2e-test/sumomo.py` の既存 `RuntimeError` メッセージは全て英語で書かれている (例: `sumomo.exe exited unexpectedly with code N`)。本 issue では既存方針を踏襲して英語のまま差し替え、`e2e-test/` 配下メッセージの日本語化は別 issue で全体方針として扱う。

## 設計方針

`returncode` を見て分岐し、`exit code 0` を本物のクラッシュと区別する。

- `returncode == 0` のとき: `sumomo.exe exited unexpectedly with code 0 (likely graceful disconnect; check sumomo logs for wscode)` という中立的表現にする。`likely` を使い「サーバ切断と決め打ちしない」中立メッセージとする (graceful disconnect 以外に内部例外や SIGTERM 等の可能性もあるため)
- `returncode != 0` のとき: 従来通り `sumomo.exe has crashed (exit code: N)` を維持する。`returncode` は Windows ランタイムで NTSTATUS 由来の大きな負数になる場合があるが、本 issue では既存の 10 進そのまま表示を踏襲する (表示形式変更は別 issue)

except 節も同じ分岐ルールで書き換え、元例外を `raise ... from e` でチェーンさせる。これに伴い現行コードの `\nOriginal error: {e}` の手動埋め込みは廃止し、Python 標準の chained traceback (`__cause__`) に任せる。

ヘルパー関数化はしない (分岐ポイントは 2 箇所のみで、インライン記述で 5 行程度に収まり可読性が高いため。YAGNI を優先)。

## 完了条件

- `e2e-test/sumomo.py:812` と `e2e-test/sumomo.py:824` の `RuntimeError` メッセージが `returncode` で分岐され、`returncode == 0` のとき "crashed" の語を含まない
- `e2e-test/sumomo.py:824` (except 節) では元例外を `raise ... from e` でチェーンしている (`\nOriginal error: {e}` の手動埋め込みは削除する)
- `uv run --directory=e2e-test ruff check sumomo.py` が緑である (`ty` は本 issue で新規導入しない、ruff のみで型表現以外の構文確認は足りる)
- `grep -nE "has crashed \\(exit code: 0\\)" e2e-test/sumomo.py` の結果が 0 件 (exit code 0 で `has crashed` を出していた旧表現が残存していないことの確認。非 0 の `has crashed` は仕様として残るためここではマッチさせない)
- 本 issue の効果検証は schedule CI のフレーキー再発時にしか実機で観測できない (exit code 0 経路は意図的に再現困難)。PR マージ前は上の lint と grep の静的確認に絞り、実機検証は次回 0004 の追跡で再発が観測されたタイミングで「has crashed (exit code: 0)」が出ていないことを確認する
- `CHANGES.md` の `## develop` 配下、既存の `### misc` セクション (`CHANGES.md:88` 付近) に以下の形式で `[FIX]` エントリを追記する。挿入位置は既存 `[CHANGE]` エントリの直後、既存 `[UPDATE]` エントリの直前の物理位置 (0003 / 0005 と同じ位置取り)。`### misc` 内既存エントリの並べ替えは本 issue のスコープ外。担当者ハンドルは PR 作成者のものに書き換える:

  ```
  - [FIX] e2e-test/sumomo.py の Windows 用 crashed 判定を改善する
    - get_stats() で exit code 0 でもプロセス終了時に "has crashed" と表示されていたのを修正する
    - exit code 0 のときは "exited unexpectedly with code 0 (likely graceful disconnect; check sumomo logs for wscode)"、非 0 のときは従来の "has crashed (exit code: N)" を出すように分岐する
    - @<PR 作成者のハンドル>
  ```

## 解決方法

`e2e-test/sumomo.py:809-812` を以下に差し替える:

```python
# Windows の場合、プロセスが生きているか確認
if self.process and platform.system().lower() == "windows":
    if self.process.poll() is not None:
        returncode = self.process.returncode
        if returncode == 0:
            raise RuntimeError(
                f"sumomo.exe exited unexpectedly with code 0 "
                f"(likely graceful disconnect; check sumomo logs for wscode)"
            )
        raise RuntimeError(f"sumomo.exe has crashed (exit code: {returncode})")
```

`e2e-test/sumomo.py:820-826` の except 節を以下に差し替える (元例外は `raise ... from e` でチェーンさせる):

```python
except Exception as e:
    # Windows の場合、エラー時にプロセスの状態を確認
    if self.process and platform.system().lower() == "windows":
        if self.process.poll() is not None:
            returncode = self.process.returncode
            if returncode == 0:
                raise RuntimeError(
                    f"sumomo.exe exited unexpectedly with code 0 while getting stats "
                    f"(likely graceful disconnect; check sumomo logs for wscode)"
                ) from e
            raise RuntimeError(
                f"sumomo.exe has crashed while getting stats (exit code: {returncode})"
            ) from e
    raise
```
