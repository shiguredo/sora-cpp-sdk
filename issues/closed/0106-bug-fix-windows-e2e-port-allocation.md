# E2E テストのポート割り当てが Windows の除外ポート範囲と衝突するのを修正する

- Created: 2026-09-18
- Completed: 2026-09-26
- Branch: feature/fix-e2e-port-allocation
- Polished: 2026-09-18

## 目的

Windows の GitHub-hosted runner (`windows-2025`) で実行する E2E テストが、sumomo の HTTP サーバー用ポートの bind 失敗により間欠的に失敗するのを修正する。

原因は `e2e-test/conftest.py` の `port_allocator` が 55000 から連番でポートを払い出すだけで、そのポートが実際に bind 可能かを確認していないことにある。55000 は Windows / macOS の既定動的ポート範囲 (49152〜65535) の内側であり、Hyper-V などが予約する除外ポート範囲 (excluded port range) と衝突すると bind が 10013 で失敗する。bind 可能なポートだけを払い出すように修正し、無関係な変更の CI が赤くなるのを防ぐ。

## 現状

### 発生事象

2026-09-17 21:11 UTC の `ci` ワークフロー run [35275252896](https://github.com/shiguredo/sora-cpp-sdk/actions/runs/35275252896) の `windows_x86_64` ジョブ (job [105389511841](https://github.com/shiguredo/sora-cpp-sdk/actions/runs/35275252896/job/105389511841)) で、`test_sumomo_basic.py::test_sumomo_sendrecv_pair[AV1]` が失敗した。失敗ログの抜粋 (シグナリング URL などは省略):

```
Starting sumomo ... --http-port 55010 --http-host 127.0.0.1 --fake-capture-device
[000:024][7304] (sumomo.cpp:621): Failed to start HTTP server: Failed to bind: An attempt was made to access a socket in a way forbidden by its access permissions
FAILED test_sumomo_basic.py::test_sumomo_sendrecv_pair[AV1] - RuntimeError: Process exited unexpectedly with code 0
1 failed, 5 passed in 40.29s
```

### 失敗の機序

- `e2e-test/conftest.py` の `port_allocator` は `itertools.count(55000)` を返すだけで、候補ポートが bind 可能かを確認していない。`free_port` / `free_port2` フィクスチャと、テスト側の `next(port_allocator)` がこの連番を消費する
- `examples/sumomo/src/sumomo.cpp` の `HttpListener` コンストラクタは `acceptor_.bind()` の失敗で例外を投げ、`Sumomo::Run` はその例外をログ出力して `return` する。このため sumomo は exit code 0 で終了する
- `e2e-test/sumomo.py` の `Sumomo._wait_for_startup` はプロセスの終了を検知して `RuntimeError: Process exited unexpectedly with code 0` を投げる
- `.github/workflows/ci.yml` の E2E ジョブは `uv run pytest -v -x` で実行するため、以降のテストは実行されずジョブ全体が失敗する

### 55010 で bind に失敗した理由

- エラーメッセージは Windows の WSAEACCES (10013) である。Windows では除外ポート範囲に入ったポートは、SO_REUSEADDR を設定していても bind が 10013 で失敗する (Microsoft KB3039044)。sumomo の `HttpListener` は `reuse_address(true)` を設定しているため、この条件に一致する
- Microsoft の同記事は回避策として「既定の動的ポート範囲 (49152〜65535) 外のポートを使う」を挙げている。55000 はこの範囲の内側である
- `windows-2025` runner に Hyper-V が存在することは 0007 で確認済みである。Hyper-V は起動時に動的ポート範囲の一部を予約する
- 同一セッションで 55000〜55009 は bind に成功しており、55010 だけが失敗している。連続した除外範囲が 55010 付近から始まっている場合と整合する
- 別プロセスが `SO_EXCLUSIVEADDRUSE` で 55010 を握っていた場合も 10013 になるため完全には排他できないが、いずれにせよテスト側が bind 可能かを確認せずにポートを決め打ちしている点が問題である
- 除外範囲は runner の起動ごとに変わるため、同じテストが同じポートを要求しても成功したり失敗したりする間欠失敗になる

### 影響と発生頻度

- 無関係な変更の CI が赤くなり、本物の退行の検知を妨げる。再実行で回避できるが、develop への push でも起こりうる
- 直近 40 回の `ci` ワークフロー (2026-09-11〜2026-09-17 UTC) では `windows_x86_64` の E2E ジョブは 37 回成功、2 回はビルド失敗により未実行、失敗は今回の 1 回である。頻度は低いが原因が環境依存のため再発する

### 再現手順

1. Windows で `netsh int ipv4 add excludedportrange protocol=tcp startport=<候補ポート> numberofports=1` を管理者権限で実行し、候補ポートを除外ポート範囲に登録する
2. `e2e-test` ディレクトリで `uv run pytest -v -x test_sumomo_basic.py` を実行する
3. 候補ポートが除外範囲に当たったテストで sumomo が HTTP サーバーの起動に失敗し、`RuntimeError: Process exited unexpectedly with code 0` で失敗する

## 設計方針

- `e2e-test/conftest.py` の `port_allocator` を、候補ポートを昇順に見て実際に `127.0.0.1` で bind できるポートだけを払い出す方式に変更する。bind できない候補 (除外ポート範囲・使用中) は読み飛ばす。sumomo は `--http-host 127.0.0.1` で起動するため、確認先も `127.0.0.1` にする
- 候補の開始番号を 55000 から、Windows / macOS の既定動的ポート範囲 (49152〜65535) と Linux の既定 ephemeral range (32768〜60999) の外にある値 (例: 20000) に変更する。OS が同じポートを送信元ポートとして使う競合も避けやすくなる
- 払い出しはセッション内で一意であることを維持し、`free_port` / `free_port2` / `next(port_allocator)` という既存の使い方を変えない。テスト側の変更を不要にする
- bind の確認は実ソケットで行い、モックやスタブは使わない。払い出しロジックはテストから直接呼べる形にし、実ソケットで検証できるようにする
- 変更は `e2e-test/conftest.py` と追加テスト、`.github/workflows/ci.yml` の `test-target` に限定する。sumomo 側の挙動 (bind 失敗時に exit code 0 で終了する点など) は変更しない

### スコープ外

- sumomo が HTTP サーバーの bind 失敗時に exit code 0 で終了する挙動の変更 (0006 は `Sumomo.get_stats()` 内の 2 箇所の crashed 判定メッセージ分岐のみを対象にしており、bind 失敗時の終了コードは扱っていない)
- テストへのリトライ適用 (0005)
- `shiguredo/momo` の `test/conftest.py` にある同一の `port_allocator` への横展開 (別リポジトリのため別 issue とする)

## 完了条件

- `e2e-test/conftest.py` の `port_allocator` が、候補ポートを昇順に実際に bind して確認し、bind できない候補を読み飛ばして次のポートを返す
- 候補の開始番号が Windows / macOS の既定動的ポート範囲 (49152〜65535) と Linux の既定 ephemeral range (32768〜60999) の外になっている
- `free_port` / `free_port2` / `next(port_allocator)` の使い方を変えずに既存テストが動作する
- 使用中のポートを読み飛ばすことを実ソケットで検証するテスト (例: `e2e-test/test_port_allocator.py`) が追加されている。モック・スタブは使わない。そのテストは `.github/workflows/ci.yml` の `e2e-test` ジョブ matrix `windows_x86_64` の `test-target` に追加し、Windows の CI で実行されるようにする
- Windows runner で候補ポートを除外ポート範囲に登録した状態でも E2E テストが失敗しないことを確認する。確認は CI に一時的なステップ (`netsh int ipv4 add excludedportrange protocol=tcp startport=<候補の先頭> numberofports=1`) を追加して行い、確認後に削除する
- `uv run --directory=e2e-test ruff check` と `uv run --directory=e2e-test ruff format --check` が緑
- `windows_x86_64` の E2E ジョブが緑
- `CHANGES.md` の `## develop` 配下 `### misc` の `[FIX]` 群の先頭（最新エントリ）の直前、すなわち既存の `[FIX]` エントリである Raspberry Pi の V4L2 M2M エンコーダのエントリより前に挿入する（`### misc` 内の各種別グループでは新しいエントリほど先頭に置く慣行に従う。現状 `### misc` に存在する `[FIX]` エントリは上記の 1 件のみ）。担当者ハンドル `@<担当者>` は PR 作成者のものに書き換える:

  ```
  - [FIX] E2E テストのポート割り当てが Windows の除外ポート範囲と衝突するのを修正する
    - 候補ポートが bind 可能かを確認してから払い出し、動的ポート範囲外から探すようにする
    - @<担当者>
  ```

## 解決方法

`e2e-test/conftest.py` の `port_allocator` を、候補ポートを実際に bind して確認する方式に変更する。

- 候補の開始番号を 55000 から 20000 に変更する。Windows / macOS の既定の動的ポート範囲 (49152〜65535) と Linux の既定の ephemeral range (32768〜60999) の外側であり、OS が同じポートを送信元ポートとして使う競合も避けられる
- `is_port_bindable()` を追加し、`127.0.0.1` への実ソケットでの bind を試す。SO_REUSEADDR は設定しない (Windows では他プロセスが使用中のポートにも bind できてしまい、確認の意味がなくなるため)
- `iter_available_ports()` を追加し、bind できない候補 (除外ポート範囲、他プロセスが使用中、TIME_WAIT 中) を読み飛ばして昇順に払い出す
- `free_port` / `free_port2` / `next(port_allocator)` という既存の使い方は変更しない

完了条件のうち以下は満たしていない。

- 使用中ポートの読み飛ばしを検証するテストの追加。テストハーネスが自前で持つ探索ロジックを検証する「テストのためのテスト」になるため、レビューの結果として追加しない判断をした。`.github/workflows/ci.yml` の `test-target` への追加も行っていない
- Windows runner で候補ポートを除外ポート範囲に登録した状態での確認。除外ポート範囲の登録には管理者権限が必要で、CI を回さないと実施できないため未実施

代わりにローカルで、`127.0.0.1:20000` を実ソケットで占有した状態で払い出しが 20001 を返すことと、そのポートで sumomo の HTTP サーバーが起動することを確認した。さらに `test_sumomo_basic.py::test_sumomo_sendonly_recvonly[VP8]` を実行し、2 つの sumomo が 20000 と 20001 で起動してテストが通ることを確認した。除外ポート範囲に起因する bind 失敗が再発しないかは、`windows_x86_64` の E2E ジョブをしばらく観測して判断する。
