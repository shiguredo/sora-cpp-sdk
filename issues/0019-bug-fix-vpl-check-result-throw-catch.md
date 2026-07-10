# VPL_CHECK_RESULT マクロが throw した例外を catch するコードが不在

- Priority: High
- Created: 2026-07-10
- Completed: {YYYY-MM-DD}
- Model: DeepSeek V4 Pro
- Branch: feature/fix-vpl-check-result-throw-catch
- Polished: 2026-07-10
## 目的

`src/hwenc_vpl/vpl_utils.h` の `VPL_CHECK_RESULT` マクロが `mfxStatus` の整数値を `throw` するが、全コールサイト (`vpl_video_encoder.cpp:536,555,575,578,752,758`, `vpl_video_decoder.cpp:191,306,309`) に `catch` ブロックが存在しない。例外が捕捉されない場合 `std::terminate()` が呼ばれプロセスがクラッシュする。

## 優先度根拠

VPL ハードウェアエンコーダ/デコーダ使用時にハードウェアエラーが発生するとプロセスが強制終了する。VPL を利用する全環境で発生しうる致命バグであり High。

## 現状

`vpl_utils.h:14-20`:

```cpp
#define VPL_CHECK_RESULT(ERR, STMT) \
  do {                              \
    mfxStatus sts = STMT;           \
    if (sts < MFX_ERR_NONE) {       \
      throw ERR;                    \
    }                               \
  } while (false)
```

`throw ERR` は `mfxStatus` (int) を投げるが、エンコーダ/デコーダの `Init()`/`Encode()`/`Decode()` には `catch(...)` すら存在しない。WebRTC コーデックフレームワークは戻り値によるエラーハンドリングを前提としており、`WEBRTC_VIDEO_CODEC_ERROR` を返すべき。

## 設計方針

`VPL_CHECK_RESULT` を throw ではなく return 文に変更し、呼び出し側で `WEBRTC_VIDEO_CODEC_ERROR` を返すように修正する。もしくは全コールサイトに `catch (int)` を追加する。

## 完了条件

- VPL エンコーダ/デコーダでハードウェアエラー発生時に `std::terminate()` が呼ばれないこと
- `VPL_CHECK_RESULT` マクロが throw ではなく return 文を使用し、呼び出し側が `WEBRTC_VIDEO_CODEC_ERROR` を返すこと
- `CHANGES.md` の `## develop` 配下、`### misc` セクションに `[FIX]` エントリを追記する:
  ```
  - [FIX] VPL_CHECK_RESULT マクロが throw した例外を catch するコードが不在だったのを修正する
    - @<担当者>
  ```
