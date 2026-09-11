# サイマルキャストで RTX を無効にすると sumomo がクラッシュする

- Created: 2026-09-11
- Completed: {YYYY-MM-DD}
- Branch: feature/fix-rtx-disabled-simulcast-crash
- Polished: {YYYY-MM-DD}

## 目的

Sora の設定で `rtx = false` にした状態で sumomo が接続時にクラッシュする事象を調査し、修正する。サイマルキャストマルチコーデック対応後のビルドで発生している。

## 現状

- `sora.conf` で `rtx = false` を設定して sumomo を起動すると、接続時にセグフォする
- lldb では `allocator.h:167` 付近で発生し、`rtc::PhysicalSocketServer::Add` または `sora::SoraClientContext::Create` に問題がありそう
- 2024.6.1 のサイマルキャストマルチコーデックが入る前の sumomo では再現しない
- rtx を true にすると再現しない。ulpfec の true / false は関係なく、video-codec の指定も関係なさそう
- 再現環境: Sora 2024.2.0-canary.13、macOS、Sora C++ SDK develop、sumomo develop、webrtc-build m126.6478.1.1
- デバッグログ: https://gist.github.com/torikizi/b73e08efd31db8b0be7f83a93d7ca1c7
- 現在の `develop` にはサイマルキャストマルチコーデックの実装がなく、専用の libwebrtc ビルドで発生した事象

## 設計方針

- サイマルキャストマルチコーデック対応の libwebrtc を用意し、`rtx = false` でクラッシュを再現してスタックトレースを取得する
- クラッシュ箇所 (`allocator.h:167` 付近) と RTX 無効時のコーデック / エンコーダーの初期化処理を切り分ける
- libwebrtc 側の修正が必要かを判断する
- 再現しない場合は、どのバージョンで解消されたかを確認してクローズする

## 完了条件

- 再現環境でクラッシュの原因が特定されていること
- 原因が SDK 側の場合、`rtx = false` でクラッシュしないこと
- libwebrtc 側の修正が必要な場合、その調査結果と判断が issue に記録されていること

## Pending 理由

- サイマルキャストマルチコーデック対応の libwebrtc が `develop` に入っておらず、現行の libwebrtc では再現確認ができない
- クラッシュが確認された環境は 2024 年時点の Sora C++ SDK / libwebrtc (m126.6478.1.1) であり、現行版での再現可否が未確認

## Pending 解除条件

- マルチコーデック対応の libwebrtc が利用でき、`rtx = false` の再現確認ができる状態になったこと
