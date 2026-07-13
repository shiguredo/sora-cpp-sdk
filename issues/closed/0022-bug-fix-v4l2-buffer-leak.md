# V4L2 AllocateVideoBuffers の途中失敗時にバッファがリークする

- Priority: High
- Created: 2026-07-10
- Completed: 2026-07-14
- Model: DeepSeek V4 Pro
- Branch: feature/fix-v4l2-buffer-leak
- Polished: 2026-07-10
## 目的

`src/v4l2/v4l2_video_capturer.cpp` の `AllocateVideoBuffers()` でバッファ割り当てが途中で失敗した場合、すでに確保済みの `_pool` 配列と `mmap` 済み領域が解放されずリークする。また `AllocateVideoBuffers()` が成功しても後続の `VIDIOC_STREAMON` が失敗した場合にも同様にリークする。いずれも `_captureStarted` ゲートによりデストラクタから `DeAllocateVideoBuffers()` に到達不能なことが根本原因。

## 優先度根拠

V4L2 デバイスのバッファ割り当て失敗時にメモリリークが発生する。Raspberry Pi 等のリソース制約環境で顕在化しやすく、長時間稼働でメモリ枯渇の原因となる。High。

## 現状

`src/v4l2/v4l2_video_capturer.cpp` の `AllocateVideoBuffers()`（314-362 行目）:

1. 330 行目: `_buffersAllocatedByDevice = rbuffer.count;` — 実際の割り当て完了前に全数を設定する
2. 333 行目: `_pool = new Buffer[rbuffer.count];` — `Buffer` は POD であり `new[]` ではメンバ (`start`, `length`) は不定値
3. 342-343 行目: `VIDIOC_QUERYBUF` 失敗時に `return false` — `_pool` および、ループ内のそれ以前の反復で mmap + QBUF まで成功したバッファの mmap 領域が未解放
4. 349-352 行目: `mmap` 失敗時に `return false` — 直前までに確保したバッファの `munmap` はループで行っているが、`_pool` の `delete[]` がないため `_pool` がリークする
5. 357-358 行目: `VIDIOC_QBUF` 失敗時に `return false` — `_pool` および全 mmap 領域が未解放

さらに `StartCapture()`（128-289 行目）:

- 265-268 行目: `AllocateVideoBuffers()` が `false` を返した場合、`return -1` し `_captureStarted` は `false` のまま
- 281-284 行目: `AllocateVideoBuffers()` 成功後でも `VIDIOC_STREAMON` が失敗した場合、同様に `_captureStarted` は `false` のまま `return -1` する。この場合 `AllocateVideoBuffers()` で確保した `_pool` + 全 mmap 領域がリークする
- デストラクタ (122-126 行目) → `StopCapture()` → `DeAllocateVideoBuffers()` は `_captureStarted` ゲート (301 行目) でスキップされる

なお `StartCapture()` の `open()` 失敗 (135-139 行目)・フォーマット未発見 (187-189 行目) 等のエラーパスでは、`_deviceFd` がデストラクタで `close` されるためファイルディスクリプタのリークは発生しない。これらは本件の対象外。

## 設計方針

### クリーンアップ方式

以下の選択肢から **方式 A を推奨する**:

| 方式 | 内容 | 長所 | 短所 |
|------|------|------|------|
| A | 各 `return false` の直前にインラインでクリーンアップ | 局所的で理解しやすい。既存の mmap 失敗パス (350 行目) が既にこのパターン | コードの重複が生じる可能性がある |
| B | goto ラベルでクリーンアップに飛ぶ | クリーンアップコードが一箇所 | C++ コードベースで goto の導入は不自然 |
| C | プライベートメソッドを新設 | 再利用可能 | 本件は単一メソッド内の修正で十分。`StartCapture()` 側の STREAMON 失敗パスでも同様のインラインクリーンアップで対応可能 |

### `_buffersAllocatedByDevice` の整合性

現状 `_buffersAllocatedByDevice` は 330 行目で割り当て前に設定される。途中失敗時は実際の割り当て成功数より大きい値が設定されており、`DeAllocateVideoBuffers()` (364-378 行目) の munmap ループ (`for (int i = 0; i < _buffersAllocatedByDevice; i++)`) で未初期化の `_pool[i].start`（不定値）に対して `munmap` が実行され未定義動作となる。

したがって `DeAllocateVideoBuffers()` を流用せず、`AllocateVideoBuffers()` 内で以下の **クリーンアップロジック** を実行する:

1. mmap + QBUF まで成功したバッファの `munmap`（mmap 失敗パス (350 行目) と同様の `for` ループで）
2. `delete[] _pool`
3. `_pool = nullptr`
4. `_buffersAllocatedByDevice = -1`（コンストラクタ初期値と一致。Step 1 の移動後は `AllocateVideoBuffers()` 内のエラーパスでは常に `-1` のままのため実質的には冗長だが、将来の安全のために明示する）

### `_buffersAllocatedByDevice` の設定タイミング

330 行目の `_buffersAllocatedByDevice = rbuffer.count;` は、ループが正常完了した **直後**（for ループの `}` である 360 行目の直後、`return true;` の前）に移動する。

### `AllocateVideoBuffers` 失敗後の `StartCapture` の処理

`StartCapture()` で `AllocateVideoBuffers()` が失敗した場合、クリーンアップは `AllocateVideoBuffers()` 内部で完結させる。`StartCapture()` 側では追加のクリーンアップは不要。

`AllocateVideoBuffers()` が成功したが後続の `VIDIOC_STREAMON` (281 行目) が失敗した場合、ストリーム開始前のため `VIDIOC_STREAMOFF` を実行する `DeAllocateVideoBuffers()` を呼び出すのは不適切（不要なエラーログが発生する）。このケースでは `return -1` の直前に `AllocateVideoBuffers()` 内と同様のインラインクリーンアップを実行する。ループ上限には `_buffersAllocatedByDevice`（Step 1 の移動により `rbuffer.count` の実割り当て数が設定済み）を使用する（`kNoOfV4L2Bufffers` は定数 4 だが `rbuffer.count` がそれより小さい場合に範囲外アクセスとなるため）:

```cpp
for (int j = 0; j < _buffersAllocatedByDevice; j++)
    munmap(_pool[j].start, _pool[j].length);
delete[] _pool;
_pool = nullptr;
_buffersAllocatedByDevice = -1;
```

なお `VIDIOC_STREAMON` 失敗時、キャプチャスレッド (271-276 行目) は既に起動済みだが、`_captureStarted` が `false` のため `CaptureProcess()` 内のバッファアクセス (415 行目) は実行されない。スレッドの停止は後続のデストラクタ → `StopCapture()` で行われる。

### エッジケースとエラーパス

以下の全エラーパスでリソース解放を保証する:

| エラーポイント | 行番号 | リークするリソース | 対応 |
|---------------|--------|-------------------|------|
| `VIDIOC_REQBUFS` 失敗 | 322-324 | なし | 対応不要。`_pool` の確保前に return false するため |
| `rbuffer.count == 0` | 327-328 | なし | 対応不要。`VIDIOC_REQBUFS` は成功しているが、Linux カーネルの V4L2 実装上 `count == 0` は返り得ない。仮に発生しても `_buffersAllocatedByDevice = 0` → STREAMON 失敗 → Step 5 のクリーンアップ（`j < 0` でループ不実行）により安全 |
| `new Buffer[N]` 失敗（`std::bad_alloc`） | 333 | なし | Step 1 の移動後は `_buffersAllocatedByDevice` が `-1` のままのため安全。`MutexLock` は RAII で解放され、デストラクタが `_deviceFd` を close する |
| `VIDIOC_QUERYBUF` 失敗 (i > 0) | 342-343 | `_pool` + mmap 領域 (0..i-1) | クリーンアップパス追加 |
| `QUERYBUF` 失敗 (i == 0) | 342-343 | `_pool`（mmap 領域なし） | クリーンアップパス追加 |
| `mmap` 失敗 (i > 0) | 349-352 | `_pool`（mmap 領域は既存コードで解放済み） | `_pool` の `delete[]` 追加 |
| `mmap` 失敗 (i == 0) | 349-352 | `_pool`（mmap 領域なし） | `_pool` の `delete[]` 追加 |
| `VIDIOC_QBUF` 失敗 (i >= 0) | 357-358 | `_pool` + mmap 領域 (0..i) | クリーンアップパス追加 |
| `VIDIOC_STREAMON` 失敗 | 281-284 | `_pool` + 全 mmap 領域 | インラインクリーンアップ追加（ループ上限に `_buffersAllocatedByDevice` を使用） |

### 後方互換

内部実装の変更のみであり、API・ABI に変更はない。

### テスト戦略

V4L2 デバイスが実機に依存するため、部分割り当て失敗を意図的に発生させる単体テストは困難。以下の方針とする:

- 実機（Raspberry Pi 等）での valgrind / AddressSanitizer によるリーク確認
- 修正後の正常パスでリグレッションがないことを既存の E2E テストで確認
- 修正範囲 (18 行以内) が明確で局所的であるため、コードレビューによる検証を主とする。レビューでは全エラーパス（エッジケース表参照）で `_pool` と mmap 領域が解放されていることを確認する

## 完了条件

- `AllocateVideoBuffers()` 内のすべての `return false` 到達時点で、その時点までに確保された以下のリソースが解放されていること:
  - `new Buffer[N]` で確保した `_pool` 配列（`delete[]` 実行）
  - mmap 済みの全バッファ領域（`munmap` 実行）
  - 解放後は `_pool = nullptr` かつ `_buffersAllocatedByDevice = -1` に設定すること
- `_buffersAllocatedByDevice = rbuffer.count;` が for ループ正常完了直後のみ実行されること（360 行目直後に移動）
- `StartCapture()` の `VIDIOC_STREAMON` 失敗パス (281-284 行目) で、全バッファの `munmap` + `delete[] _pool` + `_pool = nullptr` + `_buffersAllocatedByDevice = -1` が実行されること
- `CHANGES.md` の `## develop` 配下に `[FIX]` エントリを追記する（`### misc` セクションではなく、他の `src/` コア修正と同じブロックに追記する）:
  ```
  - [FIX] V4L2 AllocateVideoBuffers の途中失敗時にバッファがリークするのを修正する
    - @<担当者>
  ```

## 対応しない理由

- アロケート失敗は極めて稀であり、失敗時はアプリケーションが終了するため OS が全メモリを回収する。リークが蓄積するケースは実質的に存在しない
- エラーパスごとにクリーンアップコードを散在させると、今後の修正で同様のコードを追加するたびに対応漏れが発生するリスクが高まり、保守性が低下する
- 上記より、費用対効果が悪いと判断し本 issue は修正せず closed にする
