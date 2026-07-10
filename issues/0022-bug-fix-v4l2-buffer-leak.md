# V4L2 AllocateVideoBuffers の部分割り当て失敗時にバッファリーク

- Priority: High
- Created: 2026-07-10
- Completed: {YYYY-MM-DD}
- Model: DeepSeek V4 Pro
- Branch: feature/fix-v4l2-buffer-leak
- Polished: {YYYY-MM-DD}

## 目的

`src/v4l2/v4l2_video_capturer.cpp` の `AllocateVideoBuffers()` でバッファ割り当ての途中で失敗した場合、すでに確保済みの `_pool` 配列と `mmap` 済み領域が解放されずリークする。`_captureStarted` ゲートによりデストラクタからも到達不能。

## 優先度根拠

V4L2 デバイスのバッファ割り当て失敗時にメモリリークが発生する。Raspberry Pi 等のリソース制約環境で顕在化しやすく、長時間稼働でメモリ枯渇の原因となる。High。

## 現状

`src/v4l2/v4l2_video_capturer.cpp:333-359` の `AllocateVideoBuffers()`:

- 333 行目: `_pool = new Buffer[rbuffer.count]` で配列確保
- 330 行目: `_buffersAllocatedByDevice = count` を設定
- 342-343 行目: `VIDIOC_QUERYBUF` 失敗時に `return false`（`_pool` 未解放）
- 357-358 行目: `VIDIOC_QBUF` 失敗時に `return false`（`_pool` + mmap 領域未解放）

呼び出し元 `StartCapture` (265-267 行目) は `return -1` し、`_captureStarted` は `false` のまま。デストラクタ → `StopCapture()` → `DeAllocateVideoBuffers()` は `_captureStarted` ゲート (301 行目) でスキップされるため、`_pool` と mmap 領域が永久リークする。

## 設計方針

エラーリターン前に確保済みリソースを解放するクリーンアップパスを追加する。

## 完了条件

- `AllocateVideoBuffers()` の途中失敗時にすでに確保したリソースがすべて解放されること
