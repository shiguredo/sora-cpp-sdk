# Intel VPL の AV1 で小さい解像度の映像が送信できない

- Created: 2026-09-11
- Completed: {YYYY-MM-DD}
- Branch: feature/fix-vpl-av1-small-resolution
- Polished: {YYYY-MM-DD}

## 目的

Intel VPL の AV1 エンコーダーで、128x96 を下回る解像度 (例: 120x90) の映像を送ろうとすると `CreateEncoder` に失敗して映像が送信できない問題を修正する。

## 現状

- Intel VPL の AV1 で 120x90 のような小さい解像度の映像を送ろうとすると、`VplVideoEncoderImpl::CreateEncoder` で失敗して映像が送信できない
- Python SDK の E2E テストで発見され、sumomo でもサイマルキャストの送信時に同様の事象が再現する
- 通常のマルチストリーム送信では最小サイズに補正されるためか再現しない。カメラデバイスが 128x96 を下回る解像度を出せる場合に sumomo でも再現する (HD Pro Webcam C920 は最小 160x90)
- 調査の結果、Intel VPL が扱える最小サイズは 128x96 であることが分かっている
- `doc/known_issues.md` の Intel VPL の AV1 / H.265 の既知の問題 (0076) と関連する
- 対応するソース: `src/hwenc_vpl/vpl_video_encoder.cpp` の `VplVideoEncoderImpl::CreateEncoder` / `InitVpl`

## 設計方針

- 128x96 を下回る解像度が渡されたときの扱いを決める。最小サイズへ補正する、エンコードをスキップする、エラーとして扱う、のいずれかを検討する
- libwebrtc の simulcast で生成されるレイヤーの解像度が 128x96 を下回るケースを確認する
- `CreateEncoder` の失敗時にログを出して、どの解像度で失敗したかを追えるようにする
- 0076 (ストリーム数が 2 本以下のときの映像送信) と関連するため、合わせて確認する

## 完了条件

- Intel VPL の AV1 で 128x96 を下回る解像度を指定しても映像が送信されること
- 128x96 以上の解像度に回帰がないこと
- 最小解像度の扱いが issue に記録されていること

## Pending 理由

- Intel VPL の実機で小さい解像度を送信できるカメラ (HD Pro Webcam C920 など) が必要
- 128x96 を下回る解像度の扱い (補正 / スキップ / エラー) の設計判断が必要
- 0076 と関連するため、合わせた検討が必要

## Pending 解除条件

- 最小解像度を下回る場合の実装方針が決まり、Intel VPL の実機で確認できる見込みが立ったこと
