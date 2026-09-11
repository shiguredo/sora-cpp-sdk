# VPL デコーダで syncp が null のときに無限ループする

- Created: 2026-09-11
- Completed: {YYYY-MM-DD}
- Branch: feature/fix-vpl-decoder-incompatible-video-param-loop
- Polished: {YYYY-MM-DD}

## 目的

Intel VPL デコーダで `MFXVideoDECODE::DecodeFrameAsync` が syncp を返さないエラー (例: `MFX_ERR_INCOMPATIBLE_VIDEO_PARAM`) のときに `continue` し続けて無限ループする問題を修正する。Android 端末の回転時にこの経路でアプリがフリーズした事象が報告されている。

## 現状

- `src/hwenc_vpl/vpl_video_decoder.cpp` の `VplVideoDecoderImpl::Decode` は、`sts == MFX_ERR_MORE_DATA` のときは `return` するが、`!syncp` のときは `RTC_LOG(LS_WARNING)` を出して `continue` する
- `MFX_ERR_INCOMPATIBLE_VIDEO_PARAM` (-14) など、syncp を返さないエラーでも同じ入力でループし続けるため、無限ループになる
- Android 端末を回転させたときにアプリがフリーズする事象で、この経路が原因になっていた。報告元では GPU ドライバの更新で事象は解消している
- `continue` の代わりに `return` にするかの判断は保留のままになっている

## 設計方針

- `DecodeFrameAsync` の戻り値ごとの扱いを整理し、syncp を返さないエラーでは `continue` せず `return WEBRTC_VIDEO_CODEC_ERROR` する
- `MFX_ERR_INCOMPATIBLE_VIDEO_PARAM` など、解像度変更時に発生しうるエラーの扱いを確認する (解像度変更は `GetVideoParam` で検出している)
- 無限ループしないことを確認する

## 完了条件

- syncp を返さないエラーで無限ループせず、エラーとして `Decode` を抜けること
- 正常系のデコードに回帰がないこと
- Intel VPL の E2E テスト (`test_sumomo_intel_vpl.py`) が通ること

## Pending 理由

- フリーズは特定の環境 (Android 端末の回転 + AV1 + Intel VPL) で発生し、報告元では GPU ドライバの更新で解消しているため、現行環境で再現しない
- syncp を返さないエラーを `return` にしてよいか、解像度変更時の扱いを含めた実装方針の確認が必要

## Pending 解除条件

- エラー時の扱い (return / continue / 再初期化) の方針が決まったこと
- 再現または検証できる環境が見込めること
