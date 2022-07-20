# 変更履歴

- CHANGE
    - 下位互換のない変更
- UPDATE
    - 下位互換がある変更
- ADD
    - 下位互換がある追加
- FIX
    - バグ修正

## develop

## 2022.7.5 (2022-07-20)

- [FIX] サイマルキャストの場合、ネイティブバッファを I420 に変換するように修正
    - @melpon

## 2022.7.4 (2022-07-20)

- [FIX] アライメントされてないと動作しないエンコーダのために、ハードウェアエンコーダやサイマルキャストの場合は crop するように修正
    - @melpon

## 2022.7.3 (2022-07-13)

- [FIX] Rotation の解決が入ってなかったのを修正
    - @melpon

## 2022.7.2 (2022-07-12)

- [FIX] DataChannel の切れ方によってはアクセス違反になることがあったのを修正
    - @melpon

## 2022.7.1 (2022-07-11)

- [ADD] run.py に --relwithdebinfo フラグを追加
    - @melpon
- [UPDATE] libwebrtc を m103.5060.5.0 に上げる
    - @voluntas @melpon
- [FIX] boost::asio::post に strand を渡してなかったのを修正
    - @melpon

## 2022.6.2 (2022-07-03)

- [FIX] DataChannel が有効だと切断時にエラーが起きていたのを修正
    - @melpon

## 2022.6.1 (2022-06-30)

- [FIX] Jetson の AV1 HWA のデコーダがうまく動いてなかったのを修正
    - @melpon

## 2022.6.0 (2022-06-30)

- [ADD] Jetson の AV1 HWA に対応
    - @melpon

## 2022.5.0 (2022-06-22)

- [ADD] bundle_id を追加
    - @melpon
- [ADD] HTTP Proxy に対応
    - @melpon
- [CHANGE] multistream, simulcast, spotlight を optional 型に変更
    - @melpon

## 2022.4.0

- [ADD] `MacCapturer::EnumVideoDevice` を追加
    - @melpon

## 2022.3.0

- [ADD] client_id, spotlight_focus_rid, spotlight_unfocus_rid, simulcast_rid を追加
    - @melpon
- [FIX] 公開ヘッダーが NvCodec ヘッダーに依存していたのを修正
    - @melpon

## 2022.2.1

- [FIX] Windows で libmfx.lib ライブラリを要求してしまっていたのを修正
    - @melpon

## 2022.2.0

- [ADD] Intel Media SDK を使ったハードウェアエンコーダ/デコーダを実装
    - @melpon

## 2022.1.2

- [FIX] ubuntu-22.04_x86_64 のリリースバイナリが含まれていなかったのを修正
    - @melpon

## 2022.1.0

**祝リリース**
