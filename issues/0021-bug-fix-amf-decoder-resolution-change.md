# AMF デコーダの解像度変更時に InitAMF の戻り値が無視されている

- Priority: High
- Created: 2026-07-10
- Completed: {YYYY-MM-DD}
- Model: DeepSeek V4 Pro
- Branch: feature/fix-amf-decoder-resolution-change
- Polished: 2026-07-10
## 目的

`src/hwenc_amf/amf_video_decoder.cpp` の `Decode()` 内で `AMF_RESOLUTION_CHANGED` または `AMF_RESOLUTION_UPDATED` 発生時に `InitAMF()` の戻り値が無視されている。`InitAMF()` が失敗して `decoder_` が nullptr のまま次ループに入ると `decoder_->SubmitInput(nullptr)` で null deref が発生する。

## 優先度根拠

動画ストリームの解像度変更は一般的なシナリオであり、AMF デコーダ使用時に発生しうる致命バグ。High。

## 現状

`src/hwenc_amf/amf_video_decoder.cpp:192-196`:

```cpp
} else if (res == AMF_RESOLUTION_CHANGED || res == AMF_RESOLUTION_UPDATED) {
    ReleaseAMF();   // decoder_ = nullptr, context_ = nullptr
    InitAMF();      // 戻り値無視！失敗しても continue
    continue;       // 次ループで decoder_->SubmitInput(nullptr) → クラッシュ
}
```

`ReleaseAMF()` (352-361行目) は `decoder_` と `context_` を nullptr に設定する。`InitAMF()` (321-349行目) は内部で `CreateDecoder()` を呼び、失敗時に `RETURN_IF_FAILED` でエラーリターンするが、その戻り値が無視されている。

## 設計方針

`InitAMF()` の戻り値をチェックし、失敗時は `WEBRTC_VIDEO_CODEC_ERROR` を返す。

## 完了条件

- 解像度変更時に `InitAMF()` が失敗した場合、null deref せず `WEBRTC_VIDEO_CODEC_ERROR` を返すこと
- `CHANGES.md` の `## develop` 配下、`### misc` セクションに `[FIX]` エントリを追記する:
  ```
  - [FIX] AMF デコーダの解像度変更時に InitAMF の戻り値が無視されているのを修正する
    - @<担当者>
  ```
