# AMF デコーダの解像度変更時に InitAMF の戻り値が無視されている

- Priority: High
- Created: 2026-07-10
- Completed: {YYYY-MM-DD}
- Model: DeepSeek V4 Pro
- Branch: feature/fix-amf-decoder-resolution-change
- Polished: 2026-07-10

## 目的

`src/hwenc_amf/amf_video_decoder.cpp` の `Decode()` 内で `AMF_RESOLUTION_CHANGED` または `AMF_RESOLUTION_UPDATED` 発生時に `InitAMF()` の戻り値が無視されている。`InitAMF()` が失敗すると `decoder_` が nullptr のまま `continue` で while ループ先頭に戻り、`res` が `AMF_RESOLUTION_CHANGED` のままのため `decoder_->SubmitInput(buffer)` (`amf_video_decoder.cpp:183`) が呼ばれて null deref でクラッシュする。

## 優先度根拠

動画ストリームの解像度変更は一般的なシナリオであり、AMF デコーダ使用時に発生しうる致命バグ。High。

## 現状

`src/hwenc_amf/amf_video_decoder.cpp:192-196`:

```cpp
} else if (res == AMF_RESOLUTION_CHANGED || res == AMF_RESOLUTION_UPDATED) {
    // デコードするサイズが変わったらデコーダを作り直す
    ReleaseAMF();   // decoder_ = nullptr （context_ は nullptr にされない）
    InitAMF();      // 戻り値無視。失敗しても continue する
    continue;
}
```

null deref に至る経路は以下のとおり。

- `ReleaseAMF()` (`amf_video_decoder.cpp:352-361`) は `decoder_` と `polling_thread_` をリセットするが、`context_` には一切触れない。
- `InitAMF()` (`amf_video_decoder.cpp:321-350`) は内部で `CreateDecoder()` を呼び、失敗時は `RETURN_IF_FAILED` でエラーコードを返すが、`Decode()` 側でその戻り値を受け取っていない。
- `InitAMF()` が失敗した場合、`CreateDecoder()` は成功パス末尾 (`amf_video_decoder.cpp:315-316`) の `Detach()` に到達しないため `decoder_` は nullptr のまま残る。
- その状態で `continue` すると while ループ先頭 (`amf_video_decoder.cpp:179`) に戻る。`res` は `AMF_RESOLUTION_CHANGED` のままなので `if (res == AMF_REPEAT)` (`amf_video_decoder.cpp:180`) は false となり、`decoder_->SubmitInput(buffer)` (`amf_video_decoder.cpp:183`) が呼ばれて null deref する。

## 設計方針

`InitAMF()` の戻り値をチェックし、失敗時は `WEBRTC_VIDEO_CODEC_ERROR` を返す。既存マクロ `WEBRTC_RETURN_IF_FAILED` (`amf_video_decoder.cpp:50-56`) を利用できる。

```cpp
} else if (res == AMF_RESOLUTION_CHANGED || res == AMF_RESOLUTION_UPDATED) {
    // デコードするサイズが変わったらデコーダを作り直す
    ReleaseAMF();
    res = InitAMF();
    WEBRTC_RETURN_IF_FAILED(res, "Failed to re-init AMF decoder after resolution change");
    continue;
}
```

- 成功時は `res` が `AMF_OK` になり、既存の正常系と同じく次ループで `SubmitInput(buffer)` が再生成された `decoder_` に対して呼ばれる。
- エンコーダ側 `InitEncode()` (`amf_video_encoder.cpp:206-208`) も `InitAMF()` の戻り値を `if (InitAMF() != AMF_OK) return WEBRTC_VIDEO_CODEC_ERROR;` でチェックしており、方針が一致する。
- 本 issue のスコープ外: `buffer` は解像度変更前の `context_` から `AllocBuffer()` した (`amf_video_decoder.cpp:171-172`) ものであり、`InitAMF()` 成功後は新旧 `context_` の不一致が生じる。これは本 null deref とは独立した懸念であり、本 issue では戻り値チェックのみを扱う。この不一致問題は別途調査・起票を検討する。

## 完了条件

- 解像度変更時に `InitAMF()` が失敗した場合、null deref せず `WEBRTC_VIDEO_CODEC_ERROR` を返すこと
- `InitAMF()` 失敗後は `decoder_` が nullptr のままとなり、次回 `Decode()` 呼び出しは先頭のガード (`amf_video_decoder.cpp:154-156`) で `WEBRTC_VIDEO_CODEC_UNINITIALIZED` を返し、再クラッシュしないこと
- 既存の正常系（解像度変更が正常に完了するケース）の挙動を変えないこと（後方互換への影響なし）
- E2E テスト: AMD ハードウェア環境で `AMD_AMF=1 uv run --directory=e2e-test pytest test_sumomo_amd_amf.py::test_sendonly_recvonly[H264] -v -s --timeout=60` を実行し、既存のデコードが回帰していないことを確認する。`InitAMF()` が失敗する異常系はモック禁止方針によりハードウェア上で再現不可であり、次の 3 点をコードレビューで確認する: (1) `InitAMF()` 失敗時に null deref せず `WEBRTC_VIDEO_CODEC_ERROR` を返すこと、(2) `continue` に到達しないこと、(3) 次回 `Decode()` が `WEBRTC_VIDEO_CODEC_UNINITIALIZED` を返すこと
- `CHANGES.md` の `## develop` 配下（本体 SDK 用セクション。`### misc` は examples / CI 用のため使わない）に `[FIX]` エントリを追記する:
  ```
  - [FIX] AMF デコーダの解像度変更時に InitAMF の戻り値が無視されているのを修正する
    - @<担当者>
  ```
