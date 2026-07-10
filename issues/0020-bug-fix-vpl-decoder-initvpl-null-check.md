# VPL デコーダ InitVpl で CreateDecoder の nullptr 戻り値未チェック

- Priority: High
- Created: 2026-07-10
- Polished: 2026-07-10

## 目的

`src/hwenc_vpl/vpl_video_decoder.cpp` の `InitVpl()` 関数内で `CreateDecoder()` の戻り値が nullptr チェックされずに `decoder_->GetVideoParam()` が呼ばれている。エンコーダ側 (`vpl_video_encoder.cpp:739`) では正しく nullptr チェックが行われている。

## 優先度根拠

`CreateDecoder()` が全サポート解像度で失敗した場合に null deref でクラッシュする。デコーダ初期化の基本パスであり High。

## 現状

`src/hwenc_vpl/vpl_video_decoder.cpp:350-357`:

```cpp
bool VplVideoDecoderImpl::InitVpl() {
  decoder_ = CreateDecoder(session_, codec_, {{4096, 4096}, {2048, 2048}}, true,
                           &alloc_request_);
  // decoder_ が nullptr の可能性あり → 次の行で null deref
  mfxStatus sts = MFX_ERR_NONE;
  mfxVideoParam param;
  memset(&param, 0, sizeof(param));
  sts = decoder_->GetVideoParam(&param);  // ← ここでクラッシュ
```

エンコーダ側の正しい実装 (`vpl_video_encoder.cpp:736-742`):

```cpp
encoder_ = CreateEncoder(session_, codec_, width_, height_, framerate_, ...);
if (encoder_ == nullptr) {
    RTC_LOG(LS_ERROR) << "Failed to create encoder";
    return WEBRTC_VIDEO_CODEC_ERROR;
}
```

## 設計方針

エンコーダ側と同様に nullptr チェックを追加し、null 時はエラーログ出力 + false リターン。

## 完了条件

- `CreateDecoder()` が nullptr を返した場合に null deref せずエラーリターンすること
- エンコーダ側 (`vpl_video_encoder.cpp:736-742`) と同様の nullptr チェックが追加されていること
- `CHANGES.md` の `## develop` 配下、`### misc` セクションに `[FIX]` エントリを追記する:
  ```
  - [FIX] VPL デコーダ InitVpl で CreateDecoder の nullptr 戻り値未チェックを修正する
    - @<担当者>
  ```
