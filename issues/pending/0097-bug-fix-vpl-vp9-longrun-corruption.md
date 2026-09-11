# Intel VPL VP9 で長時間配信すると映像が乱れる

- Created: 2026-09-11
- Completed: {YYYY-MM-DD}
- Branch: feature/fix-vpl-vp9-longrun-corruption
- Polished: {YYYY-MM-DD}

## 目的

Intel VPL の VP9 エンコーダーで長時間配信したときに映像が乱れる問題を調査し、修正する。Momo の Intel VPL 実装は C++ SDK そのままなので、C++ SDK 側で調査する。

## 現状

- Intel VPL の VP9 で長時間配信すると映像が乱れる
- ソフトウェア (libvpx) でテストした場合は問題なく、Intel VPL の VP9 の問題であることを確認している
- Momo (Intel VPL の実装は C++ SDK そのまま) で発生している
- 対応するソース: `src/hwenc_vpl/vpl_video_encoder.cpp` の `VplVideoEncoderImpl`

## 設計方針

- Intel VPL の VP9 エンコーダーで長時間動作させたときの映像の乱れを再現する
- レート制御、キーフレーム、参照フレームの扱いを確認し、`VplVideoEncoderImpl` の実装と VPL の使い方に問題がないかを切り分ける
- GPU ドライバ依存の可能性を確認する
- 修正が VPL のパラメーター調整で可能かを検討する

## 完了条件

- Intel VPL の VP9 で長時間配信しても映像が乱れないこと
- 再現手順と確認結果が issue に記録されていること
- libvpx など既存の経路に回帰がないこと

## Pending 理由

- Intel VPL の実機と長時間の配信テストが必要
- GPU ドライバに依存する可能性があり、原因の切り分けに時間がかかる
- 再現手順と確認方法の確立が必要

## Pending 解除条件

- Intel VPL の実機で長時間配信の再現と確認ができる見込みが立ったこと
