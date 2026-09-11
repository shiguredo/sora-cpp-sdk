# Intel VPL で MJPEG の HWA デコードに対応する

- Created: 2026-09-11
- Completed: {YYYY-MM-DD}
- Branch: feature/add-mjpeg-hwa-intel-vpl
- Polished: {YYYY-MM-DD}

## 目的

Intel VPL 環境 (N100 など) で、MJPEG を出力するカメラの映像を VPL の JPEG デコーダーで HWA デコードできるようにする。MJPEG 入力をソフトウェアデコードせずに扱えるようにし、N100 のような省電力環境で負荷を下げる。

## 現状

- NVIDIA では `src/camera_device_capturer.cpp` の `CreateCameraDeviceCapturer` が `use_native` 指定時に `NvCodecV4L2Capturer` を生成し、`src/hwenc_nvcodec/nvcodec_v4l2_capturer.cpp` の `NvCodecV4L2Capturer::OnCaptured` が `NvCodecDecoderCuda` (`CudaVideoCodec::JPEG`) で MJPEG を NV12 にデコードしてから OnFrame している
- Intel VPL には相当する経路がない。`CreateCameraDeviceCapturer` の `USE_VPL_ENCODER` 環境では `V4L2VideoCapturer` が使われ、MJPEG は `V4L2VideoCapturer::OnCaptured` の `libyuv::ConvertToI420` でソフトウェアデコードされる
- `src/hwenc_vpl/vpl_video_decoder.cpp` の `VplVideoDecoderImpl` は H.264 / H.265 / AV1 / VP9 を対象とし、`MFX_CODEC_JPEG` は扱っていない
- VPL には JPEG デコーダーの定義 (`vpl/mfxjpeg.h` の `MFX_CODEC_JPEG`) が存在する
- `V4L2VideoCapturer` の `use_native` は NVIDIA の MJPEG HWA 経路を選ぶための設定であり、Intel VPL では使われていない
- MJPEG HWA の親 issue では JetPack / NVIDIA Video Codec SDK は対応済みで、Intel VPL が残っている

## 設計方針

NVIDIA の実装を参考に、Intel VPL 版の MJPEG デコード経路を追加する。具体的な組み込み先は実装時に決める。

- `V4L2VideoCapturer` を継承した VPL 版キャプチャラ (例: `VplV4L2Capturer`) を `src/hwenc_vpl/` に追加し、`OnCaptured` をオーバーライドして VPL の JPEG デコーダーで MJPEG を NV12 にデコードする
- VPL の JPEG デコーダーは `MFX_CODEC_JPEG` を使い、`VplVideoDecoderImpl` に追加するか、MJPEG 専用のデコーダーとして実装するかを決める
- `src/camera_device_capturer.cpp` の `CreateCameraDeviceCapturer` に `USE_VPL_ENCODER` かつ `use_native` の場合の分岐を追加し、VPL 版キャプチャラを生成する
- V4L2 の MJPEG バッファ検証 (SOI マーカー `0xffd8` と EOS `0xffd9` の探索) は既存の `V4L2VideoCapturer::CaptureProcess` の処理を再利用する

## 完了条件

- Intel VPL 環境で MJPEG カメラの映像を VPL の JPEG デコーダーでデコードし、Sora へ送信できること
- MJPEG 以外の入力形式に回帰がないこと
- `CHANGES.md` の `## develop` に `[ADD]` エントリを追記していること

## Pending 理由

- MJPEG の HWA デコードをどの層に組み込むか (V4L2 キャプチャラの派生か、`VplVideoDecoderImpl` の拡張か、専用デコーダーか) が未確定であり、実装方針の設計判断が必要
- Intel VPL の JPEG デコードは N100 などの実機での動作確認が必要で、検証環境がまだ用意できていない
- 親 issue の umbrella 配下の対応であり、起票から時間が経っても対応時期が決まっていない

## Pending 解除条件

- VPL の JPEG デコード経路の組み込み先と実装方針が確定したこと
- N100 などの実機で VPL の JPEG デコーダーが利用できることを確認できたこと
- 実装の優先度が決まったこと
