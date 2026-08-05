# VideoEncoder がフレームドロップを OnFrameDropped() で通知しない

- Created: 2026-08-04
- Completed: {YYYY-MM-DD}
- Branch: feature/fix-video-encoder-frame-drop-notification
- Polished: 2026-08-05
- Reporter: @torikizi

## 目的

libwebrtc の VideoEncoder は、エンコーダーがフレームをドロップした場合に `EncodedImageCallback::OnFrameDropped()` で通知し、送信フレームの `EncodedImage` に `is_end_of_temporal_unit` を設定することを求めている (issuetracker.google.com/issues/467444018)。この要件は m149 時点で既に libwebrtc に入っており、現在の SDK が利用する m150 にも含まれているため、ビルドエラーやクラッシュは発生しない。しかし独自 VideoEncoder 実装 (HWA と OpenH264) ではフレームドロップ時の通知が行われず、フレームドロップ統計などの機能が期待する動作をしていない。OpenH264 エンコーダーは参照実装 (libwebrtc の h264_encoder_impl) に合わせ、HWA エンコーダーは libwebrtc の要求どおりに修正する。

本 issue の対象は SDK 独自の VideoEncoder 実装 (OpenH264 と HWA の 4 種) のみであり、libwebrtc 内蔵のエンコーダー (VP8 / VP9 / AV1、macOS / iOS の VideoToolbox など) は対象外である。

## 現状

エンコーダー実装に `EncodedImageCallback::OnFrameDropped()` の呼び出しと `EncodedImage::set_end_of_temporal_unit()` の設定が存在しない。

- `OpenH264VideoEncoder::Encode()`
  - エンコーダー内部のフレームスキップ (`size() == 0`) で `OnFrameDropped()` を呼ばず、送信フレームに `is_end_of_temporal_unit` を設定しない。参照実装 (libwebrtc の h264_encoder_impl) はこの経路で `OnFrameDropped()` を呼ぶ
  - `kEmptyFrame` スキップは参照実装と同じく通知しないため対象外
- `NvCodecVideoEncoderImpl::Encode()` / `VplVideoEncoderImpl::Encode()` / `AMFVideoEncoderImpl::ProcessBuffer()`
  - AV1 SVC の `layer_frames.empty()` 経路で通知なしに正常終了を返す (AMF は `AMF_OK`)。この経路は HWA 独自の実装であり、参照実装 (libaom の AV1 エンコーダー) の `layer_frames.empty()` はエラーとして扱われる
- `V4L2H264Encoder::SendFrame()`
  - 送信フレームの `EncodedImage` に `is_end_of_temporal_unit` を設定していない

### この状態による影響

- `OnFrameDropped()` を呼ばないため、エンコーダーがフレームをドロップしても libwebrtc 側に通知されない。フレームドロップ統計 (`frames_dropped_by_encoder`) が更新されず、品質適応のリソース管理 (解像度・フレームレートの適応判断) にもエンコーダードロップが反映されない
- `is_end_of_temporal_unit` を設定しないため、WebRTC の送信側がテンポラルユニット (同じ瞬間の映像を表すフレームのまとまり) の完了を認識できない。SVC / サイマルキャストで一部レイヤーのフレームが出力されなかった際、そのレイヤーのドロップ統計が正しく更新されない

## 設計方針

変更対象:

- `OpenH264VideoEncoder::Encode()`
- `NvCodecVideoEncoderImpl::Encode()`
- `VplVideoEncoderImpl::Encode()`
- `AMFVideoEncoderImpl::Encode()` / `ProcessBuffer()`
- `V4L2H264Encoder::SendFrame()`

参照実装 (SDK が利用する libwebrtc のソース内):

- `h264_encoder_impl` (OpenH264 エンコーダー)
- `libaom_av1_encoder` (AV1 エンコーダー)

- `OpenH264VideoEncoder::Encode()` は参照実装 (h264_encoder_impl) に合わせる
  - 送信レイヤー数 (`sending=false` のレイヤーを除く) を事前集計し、各送信レイヤーで減算した結果を `set_end_of_temporal_unit(num_layers_to_send == 0)` としてドロップ判定 (`size() == 0`) より前に設定する
  - エンコーダー内部のフレームスキップ (`size() == 0`) で `OnFrameDropped()` を呼ぶ。引数は参照実装と同様に `RtpTimestamp()` / `SimulcastIndex()` / `is_end_of_temporal_unit()` から取る
  - 事前集計と減算の整合を `RTC_DCHECK_EQ(num_layers_to_send, 0)` で検出する
  - `frame_types` の参照インデックスを参照実装どおり `simulcast_idx` に統一する (現状は `kEmptyFrame` 判定が `i`、キーフレーム判定が `simulcast_idx` と食い違っている)
- 単一レイヤーの HWA エンコーダー (`NvCodecVideoEncoderImpl` / `VplVideoEncoderImpl` / `AMFVideoEncoderImpl` / `V4L2H264Encoder`) は、送信フレームの `EncodedImage` に `set_end_of_temporal_unit(true)` を設定する。H.264 / H.265 / VP8 / VP9 は単一テンポラルレイヤーで運用される (1 入力 = 1 出力 = 1 テンポラルユニット) ため true で正しい。AV1 も参照実装 (libaom の AV1 エンコーダー) が単一空間レイヤー時に `sid == num_spatial_layers - 1` の評価結果として true を設定していることと一致する。なお本実装は `layer_frames[0]` の 1 出力のみを扱い空間レイヤー分割に対応していないため、AV1 は単一空間レイヤー (L1T1 / L1T2 / L1T3) 前提で運用される。空間レイヤー複数 (L2T1 等) は本実装の既知の制約であり、その場合の `is_end_of_temporal_unit` 判定は対象外。V4L2 はドロップを timestamp 不一致で間接的に検知できるのみで、ドロップしたフレームを特定できないため、通知の追加は行わない
- `NvCodecVideoEncoderImpl` / `VplVideoEncoderImpl` / `AMFVideoEncoderImpl` の AV1 SVC `layer_frames.empty()` 経路で `OnFrameDropped(rtp_timestamp, 0, true)` を呼んでから正常終了を返す。現実装は各 `Encode()` で `layer_frames[0]` の 1 出力のみを扱い空間レイヤー分割を行わないため、spatial_id は 0 で正しい。rtp_timestamp は NvCodec / VPL では `frame.rtp_timestamp()`、AMF では `ProcessBuffer()` がバッファのプロパティから取得した値を用いる。AMF の `ProcessBuffer()` は polling スレッドから呼ばれるため、`OnFrameDropped()` は既存の `OnEncodedImage()` 呼び出しと同様に `mutex_` 保護下で取得した callback を経由して呼ぶ。ハードウェアのレート制御によるドロップは、出力が発生しない事象をバッファリング待ちなどと区別して確定できないため通知対象外
- 各エンコーダーは `SimulcastEncoderAdapter` 経由で利用される。単一ストリーム (bypass) 時はコールバックが素通しになり本修正がそのまま有効で、サイマルキャスト時は HWA (非 bypass) ではアダプタ側が `is_end_of_temporal_unit` を再計算し、OpenH264 (サイマルキャストでも bypass) ではエンコーダー自身の集計が使われる。どちらの構成でも正しく動作する

## 完了条件

- `OpenH264VideoEncoder` のエンコーダー内部スキップ (`size() == 0`) と `NvCodecVideoEncoderImpl` / `VplVideoEncoderImpl` / `AMFVideoEncoderImpl` の AV1 SVC `layer_frames.empty()` 経路で `OnFrameDropped()` が呼ばれること
- OpenH264 と HWA エンコーダー (H.264 / H.265 / VP8 / VP9 / AV1) の送信フレームの `EncodedImage` に `is_end_of_temporal_unit` が設定されること (AV1 は単一空間レイヤー前提、空間レイヤー複数は本実装の既知の制約として対象外)
- `OnFrameDropped()` の呼び出しと `is_end_of_temporal_unit` の設定の 2 点は、ドロップ経路が E2E で決定的に再現できないため、コードレビューで確認する (参照実装の単体テストはモックベースのため、モック・スタブ禁止の規約に従い SDK には導入しない)
- ローカルビルド (`python3 run.py build --test --disable-cuda macos_arm64`) と既存テストが通ること (回帰がないこと)。OpenH264 の事前集計 (`num_layers_to_send`) は参照実装どおり減算され、デバッグビルドで `RTC_DCHECK_EQ(num_layers_to_send, 0)` が発火しないこと
- E2E テストの `test_sumomo_openh264_with_simulcast` が通ること (OpenH264 のサイマルキャスト挙動の回帰確認)
- 対応するハードウェア環境で各 HWA の E2E テスト (NVIDIA / Intel VPL / AMD AMF / Raspberry Pi V4L2) が通ること
- `python3 run.py format` で clang-format に差分が出ないこと
- 変更履歴 (`CHANGES.md`) の `## develop` セクションのコア SDK の `[FIX]` 群 (`### misc` サブセクションではなく、`## develop` 直下の `[FIX]` 群) の先頭に次を追記する

  ```text
  - [FIX] VideoEncoder がフレームドロップを OnFrameDropped() で通知しない問題を修正する
    - libwebrtc が要求するフレームドロップ通知とテンポラルユニット境界の設定が独自エンコーダーで行われていなかったのを修正する
    - OpenH264 エンコーダーは参照実装 (libwebrtc の h264_encoder_impl) に合わせ、送信レイヤー数の事前集計で最後のレイヤーに `set_end_of_temporal_unit` を設定し、エンコーダー内部のフレームスキップ時に `OnFrameDropped()` を呼ぶようにする
    - NvCodec / VPL / AMF / V4L2 の HWA エンコーダーは送信フレームに `set_end_of_temporal_unit(true)` を設定する
    - NvCodec / VPL / AMF の AV1 SVC は `layer_frames.empty()` 経路のフレームドロップを `OnFrameDropped()` で通知する
    - 参考 : libwebrtc で `OnFrameDropped()` が追加されたコミットのリンク
      - https://source.chromium.org/chromium/_/webrtc/src/+/54ff9c19789b36a18d5ad9576be3775255caa279
    - @<担当者>
  ```
