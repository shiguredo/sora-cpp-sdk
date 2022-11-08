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

## 2022.16.0 (2022-11-08)

- [CHANGE] Boost.Filesystem への依存を追加
    - @melpon
- [ADD] オーディオコーデック Lyra に対応
    - @melpon

## 2022.15.1 (2022-11-02)

- [FIX] CI で .env ファイル名の誤りを修正する
    - @miosakuma

## 2022.15.0 (2022-11-02)

- [UPDATE] libwebrtc を `M107.5304@{#4}` に上げる
    - @torikizi
- [FIX] 廃止になった `audio_opus_params_clock_rate` を削除する
    - @torikizi
- [FIX] CI で Windows の場合 $GITHUB_OUTPUT に "\r" が混入するのを除去する
    - @miosakuma

## 2022.14.0 (2022-10-05)

- [ADD] SoraSignaling に `GetConnectionID`, `GetConnectedSignalingURL`, `IsConnectedDataChannel`, `IsConnectedWebsocket` 関数を追加
    - @melpon

## 2022.13.0 (2022-10-05)

- [CHANGE] `ScalableVideoTrackSource::OnCapturedFrame` の戻り値を void から bool にする
    - @melpon
- [ADD] SoraSignalingConfig に `audio_opus_params_clock_rate`, `signaling_notify_metadata`, `disable_signaling_url_randomization` を追加
    - @melpon

## 2022.12.3 (2022-10-03)

- [UPDATE] Boost のビルド済みバイナリに Filesystem を追加
    - @melpon

## 2022.12.2 (2022-10-02)

- [UPDATE] iOS の IPHONE_DEPLOYMENT_TARGET を 10.0 から 13.0 に上げる
    - @melpon

## 2022.12.1 (2022-09-24)

- [FIX] ログが毎フレーム出力されてしまっていたのを修正
    - @melpon

## 2022.12.0 (2022-09-24)

- [CHANGE] デバイスからキャプチャしたフレームをコールバックする機能を追加
    - これに伴ってデバイス生成時のパラメータが変わるので下位互換性は無くなる
    - @melpon

## 2022.11.1 (2022-09-14)

- [FIX] Android で SoraDefaultClient が使えなかったのを修正
    - @melpon
- [FIX] SoraDefaultClientConfig の `use_audio_deivce` はスペルミスなので `use_audio_device` に修正
    - @melpon

## 2022.11.0 (2022-09-09)

- [CHANGE] Jetson の HW MJPEG デコーダを一時的に無効にする
    - @melpon
- [CHANGE] OnSetOffer に type: offer メッセージ全体を渡す
    - @melpon
- [UPDATE] libwebrtc を m105.5195.0.0 に上げる
    - @voluntas @melpon
- [UPDATE] boost を 1.80.0 に上げる
    - @voluntas
- [UPDATE] Android NDK を r25b に上げる
    - @melpon

## 2022.10.1 (2022-08-15)

- [CHANGE] Android の要求バージョンを `24` から `29` に上げる
    - @melpon
- [UPDATE] libwebrtc を m104.5112.7.0 に上げる
    - @melpon
- [ADD] Sora C++ SDK のリリースに Boost パッケージを含める
    - @melpon

## 2022.9.0 (2022-07-29)

- [CHANGE] DataChannel の準備が完了したことを示す通知を追加
    - 純粋仮想関数を追加したため破壊的変更となる
    - @melpon
- [ADD] mid を取得する関数を追加
    - @melpon

## 2022.8.0 (2022-07-28)

- [CHANGE] Intel Media SDK を利用したハードウェアエンコーダ/デコーダを削除
    - @melpon
- [ADD] oneVPL を利用したハードウェアエンコーダ/デコーダを追加
    - @melpon

## 2022.7.8 (2022-07-25)

- [FIX] NVDEC のデコードの画像サイズが変わった時に即時で追従するように修正
    - @melpon

## 2022.7.7 (2022-07-23)

- [FIX] カメラの無い Android 端末でカメラを利用しようとすると落ちるのを修正
    - @melpon

## 2022.7.6 (2022-07-23)

- [FIX] デコードする画像が小さすぎる場合に NVDEC がエラーを吐いて止まっていたのを修正
    - @melpon
- [FIX] NVDEC のフレームがロックされていなくて未定義動作を起こしていたのを修正
    - @melpon

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
