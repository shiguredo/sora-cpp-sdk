# VPL デコーダ InitVpl で CreateDecoder の nullptr 戻り値未チェック

- Priority: High
- Created: 2026-07-10
- Completed: {YYYY-MM-DD}
- Model: DeepSeek V4 Pro
- Branch: feature/fix-vpl-decoder-initvpl-null-check
- Polished: 2026-07-10
## 目的

`src/hwenc_vpl/vpl_video_decoder.cpp` の `VplVideoDecoderImpl::InitVpl()` 内で `CreateDecoder()` の戻り値が nullptr チェックされないまま `decoder_->GetVideoParam()` が呼ばれており、`CreateDecoder()` が nullptr を返すと null deref でクラッシュする。エンコーダ側の `VplVideoEncoderImpl::InitVpl()` では正しく nullptr チェックが行われているため、これに倣ってデコーダ側にも nullptr チェックを追加する。

## 優先度根拠

`CreateDecoder()` は指定した全解像度 (`{4096, 4096}`, `{2048, 2048}`) で `Query()` / `Init()` が失敗すると nullptr を返す (`vpl_video_decoder.cpp:117`)。VPL デコーダが利用できない環境やハードウェアエラー時にこのパスを通り、デコーダ初期化の基本パスで確実にクラッシュする。回避不能な致命バグであり High。

## 現状

`src/hwenc_vpl/vpl_video_decoder.cpp:349-357`:

```cpp
bool VplVideoDecoderImpl::InitVpl() {
  decoder_ = CreateDecoder(session_, codec_, {{4096, 4096}, {2048, 2048}}, true,
                           &alloc_request_);

  mfxStatus sts = MFX_ERR_NONE;

  mfxVideoParam param;
  memset(&param, 0, sizeof(param));
  sts = decoder_->GetVideoParam(&param);
```

`CreateDecoder()` (`vpl_video_decoder.cpp:103-118`) は全解像度で失敗すると nullptr を返すが、戻り値をチェックせず 357 行目で `decoder_->GetVideoParam()` を呼んでいるため、nullptr の場合ここで null deref する。357 行目直後の `sts != MFX_ERR_NONE` チェックは `GetVideoParam()` 呼び出し後の判定であり、nullptr を防げない。

エンコーダ側の `VplVideoEncoderImpl::InitVpl()` (`vpl_video_encoder.cpp:735-742`) では、`CreateEncoder()` の直後に nullptr チェックが入っている:

```cpp
  encoder_ = CreateEncoder(session_, codec_, width_, height_, framerate_,
                           bitrate_adjuster_.GetAdjustedBitrateBps() / 1000,
                           max_bitrate_bps_ / 1000, true);
  if (encoder_ == nullptr) {
    RTC_LOG(LS_ERROR) << "Failed to create encoder";
    return WEBRTC_VIDEO_CODEC_ERROR;
  }
```

ただし、デコーダの `InitVpl()` は `bool` を返し (`Configure()` (`vpl_video_decoder.cpp:212-218`) から `return InitVpl();` で呼ばれる)、エンコーダの `InitVpl()` は `int32_t` (`WEBRTC_VIDEO_CODEC_*`) を返す点が異なる。

## 設計方針

`CreateDecoder()` 呼び出しの直後、`mfxStatus sts` の宣言より前に nullptr チェックを追加する。デコーダの `InitVpl()` は `bool` を返すため、エンコーダ側の `WEBRTC_VIDEO_CODEC_ERROR` ではなく `false` を返す:

```cpp
  decoder_ = CreateDecoder(session_, codec_, {{4096, 4096}, {2048, 2048}}, true,
                           &alloc_request_);
  if (decoder_ == nullptr) {
    RTC_LOG(LS_ERROR) << "Failed to create decoder";
    return false;
  }
```

`InitVpl()` が `false` を返すと `Configure()` も `false` を返し、WebRTC のデコーダ初期化失敗として扱われる。以降 `Decode()` が呼ばれても `decoder_ == nullptr` チェック (`vpl_video_decoder.cpp:223`) により `WEBRTC_VIDEO_CODEC_UNINITIALIZED` を返すため、追加のガードは不要。純粋な防御的追加であり、`CreateDecoder()` が成功する正常系の挙動には影響しない (後方互換性への影響なし)。

## 完了条件

- `CreateDecoder()` が nullptr を返した場合に null deref せず、`RTC_LOG(LS_ERROR)` でエラーログを出力して `false` を返すこと（デコーダの `InitVpl()` は `bool` を返すため、エンコーダ側の `WEBRTC_VIDEO_CODEC_ERROR` ではなく `false` を返す）
- nullptr チェックが `CreateDecoder()` 呼び出しの直後、`decoder_->GetVideoParam()` 呼び出しより前に追加されていること
- 全解像度で `CreateDecoder()` が失敗する状況でしか発火しないため単体テストでの再現は困難である。`tests/` 配下に VPL デコーダを対象とした既存テストはないため、既存の E2E テスト (`INTEL_VPL=1` の `test_sumomo_intel_vpl.py`) または VPL 非搭載環境での手動確認でクラッシュしないことを確認する
- `CHANGES.md` の `## develop` 直下（`### misc` セクションより前）に `[FIX]` エントリを追記する。`### misc` は Examples / CI / tooling 用のため使わない:
  ```
  - [FIX] VPL デコーダ `InitVpl` で `CreateDecoder` の nullptr 戻り値未チェックを修正する
    - @<担当者>
  ```
