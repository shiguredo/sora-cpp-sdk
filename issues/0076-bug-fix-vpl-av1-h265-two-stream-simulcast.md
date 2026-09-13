# Intel VPL の AV1 / H.265 でサイマルキャスト時にストリーム数が 2 本以下だと映像が送信できない

- Created: 2026-09-11
- Completed: YYYY-MM-DD
- Branch: feature/fix-vpl-av1-h265-two-stream-simulcast
- Polished: 2026-09-13
- Reporter: @torikizi

## 目的

Intel VPL (libvpl) のハードウェアエンコーダーで AV1 / H.265 のサイマルキャストを行うとき、Sora が払い出すストリーム数が 2 本以下になる解像度とビットレートを指定すると、送信側で映像が送信されず受信側に表示されない。この問題を修正し、ストリーム数が 2 本以下の構成でも映像を送信できるようにする。

Python SDK の e2e テストで発見された問題であり、利用者が同じ構成を選ぶと映像が出ないため、修正または回避策の提示が必要である。

## 現状

### 発生事象

- Intel VPL + H.265 のサイマルキャストで、解像度 640 x 360 / ビットレート 700 (Sora が払い出すストリーム数が 2 本) を指定すると、受信側で映像が表示されない
- ストリーム数が 3 本になる解像度とビットレート (例: 960 x 540 / ビットレート 3000) では発生しない
- 同じ Intel VPL を使う H.264 では発生せず、H.265 と AV1 に限定される
- HD (ストリーム数が 3 本以上) でも発生しない

### 再現手順

Python SDK の e2e テストで使われた設定は次のとおりである。解像度とビットレートの組み合わせが 2 本ストリームになる場合に再現する。

```python
# 360p
("H265", "libvpl", 700, 640, 360, 2),

sendonly = SoraClient(
    signaling_urls,
    SoraRole.SENDONLY,
    channel_id,
    simulcast=True,
    audio=False,
    video=True,
    video_codec_type=video_codec_type,
    video_bit_rate=video_bit_rate,
    metadata=metadata,
    video_width=video_width,
    video_height=video_height,
    use_hwa=True,
)
```

### 確認環境

- Ubuntu 24.04
- GMKtec
  - Intel Core Ultra 5 125H
  - Intel Graphics Version 24.48.47.5
  - Intel Arc Graphics Driver Version 32.0.101.6325
- カーネル: `Linux <hostname> 6.8.0-52-generic #53-Ubuntu SMP PREEMPT_DYNAMIC Sat Jan 11 00:06:25 UTC 2025 x86_64 x86_64 x86_64 GNU/Linux`

### 既知の問題としての記載

修正までに時間がかかるため、`doc/known_issues.md` に既知の問題として記載済みである (commit `e2e1db7`)。現状の記載は次のとおりである。

> 現在 Intel VPL で H.265 のサイマルキャストを行う場合、ストリーム数が 2 本以下になる解像度とビットレートを指定すると映像を送信できません。
> そのため、 H.265 のサイマルキャストを利用する場合は、ストリーム数が 3 本になる解像度とビットレートを指定する必要があります。
> または、H.265 以外のコーデックを使用することでこの問題を回避できます。

## 設計方針

原因は未特定であり、まず Intel VPL のエンコーダー実装を調査して、ストリーム数が 2 本以下の構成で何が起きているかを特定する。対象は `src/hwenc_vpl/vpl_video_encoder.cpp` の `VplVideoEncoderImpl` とし、`SimulcastEncoderAdapter` からレイヤーごとに渡される `webrtc::VideoCodec` をもとに `mfxVideoParam` を組み立てる初期化処理を中心に確認する。なお、`VplVideoEncoderImpl::InitEncode` は旧シグネチャ (`VideoCodec` のみを受け取る) を実装しており、`VideoEncoder::Settings` はこの初期化パスでは使われない。

見るべき観点は次のとおりである。

- 解像度 (640 x 360) とビットレート (700) から決まる `mfxVideoParam` の `mfx.FrameInfo` (Width / Height / CropW / CropH) とレート制御 (`mfx.RateControlMethod` など) が 2 本ストリーム時に不正または非対応の値になっていないか
- 関連する issue である `issues/pending/0100-bug-fix-vpl-av1-small-resolution.md` では、Intel VPL が扱える最小解像度が 128 x 96 であることが調査で判明している。サイマルキャストで生成されるレイヤーの解像度がこの下限を下回っていないかも確認する
- H.265 と AV1 で共通の原因か、AV1 の `svc_controller_` (`webrtc::ScalableVideoController`) に起因する個別の原因かを切り分ける
- VPL が解像度・ビットレートの組み合わせを拒否した場合に `InitVpl()` がエラーをどう扱い、どこで送信が止まるか
- 受信側で映像が出ないことから、エンコード自体が失敗しているのか、RTP 送出が止まっているのかを統計 (`outbound-rtp`) で切り分ける

特定できた原因に対して、VPL のパラメーター調整または該当構成の検出と回避で修正する。

## 完了条件

- Intel VPL の H.265 / AV1 サイマルキャストで、ストリーム数が 2 本以下になる解像度とビットレートを指定しても映像が送信されること
- ストリーム数が 3 本の既存挙動に回帰がないこと (Intel VPL の E2E テスト `test_sumomo_intel_vpl.py` の `test_simulcast` が通ること)
- `doc/known_issues.md` から Intel VPL の AV1 / H.265 に関する既知の問題の記載を削除できること
- `CHANGES.md` の `## develop` セクションの `[FIX]` に修正エントリを追記すること
