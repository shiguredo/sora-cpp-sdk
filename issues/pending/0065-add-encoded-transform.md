# WebRTC Encoded Transform に対応する

- Created: 2026-09-10
- Completed: {YYYY-MM-DD}
- Branch: feature/add-encoded-transform
- Polished: {YYYY-MM-DD}

## 目的

Sora C++ SDK でエンコード後・RTP パケタライズ前の音声・映像ペイロードにアプリがアクセスできるようにする。E2EE をはじめとするペイロードの加工・暗号化を SDK として扱えるようにするため。

## 現状

- `src/` と `include/` に Encoded Transform / E2EE 関連の実装は存在しない。`webrtc::FrameEncryptorInterface` / `webrtc::FrameDecryptorInterface` / `webrtc::FrameTransformerInterface` を扱うコードもない
- libwebrtc (WEBRTC_BUILD_VERSION=m154.8037.0.0) の公開 API には Encoded Transform 関連の型が含まれている
  - `api/crypto/frame_encryptor_interface.h` の `webrtc::FrameEncryptorInterface`
  - `api/crypto/frame_decryptor_interface.h` の `webrtc::FrameDecryptorInterface`
  - `api/frame_transformer_interface.h` の `webrtc::FrameTransformerInterface` / `webrtc::TransformableVideoFrameInterface` / `webrtc::TransformableAudioFrameInterface`
  - `api/sframe/sframe_encrypter_interface.h` / `api/sframe/sframe_decrypter_interface.h` の SFrame
- `webrtc::RtpSenderInterface::SetFrameEncryptor` / `SetFrameTransformer` と `webrtc::RtpReceiverInterface::SetFrameDecryptor` / `SetFrameTransformer` は公開 API として提供されている
- `include/sora/sora_signaling.h` の `SoraSignaling::GetPeerConnection` は公開されており、アプリは `webrtc::PeerConnectionInterface::AddTrack` の戻り値から `webrtc::RtpSenderInterface` を取得できる
- 同ヘッダの `SoraSignalingObserver::OnTrack` は `webrtc::RtpTransceiverInterface` を受け取るため、アプリは `transceiver->receiver()` から `webrtc::RtpReceiverInterface` を取得できる
- SDK 側に専用 API はないが、アプリが上記の公開 API を直接呼ぶことで Encoded Transform / E2EE を実装することは現状でも可能
- Python SDK (sora-python-sdk) は sora-rust-sdk 経由で WebRTC Encoded Transform に対応している。音声の frame transformer は未対応で、映像のみ対応している

## 設計方針

- `SoraSignalingConfig` に暗号化器・復号器・変換器を渡す項目を追加するか、アプリが公開 API を直接呼ぶ運用のままとするかを決める
- SDK が対応する場合、送信側は `SoraSignaling::GetPeerConnection` の `AddTrack` の戻り値、受信側は `OnTrack` の `transceiver->receiver()` に対してフレーム暗号化器・復号器・変換器を設定する経路を提供する
- 音声・映像の両方に対応する
- E2EE の鍵管理や SFrame の暗号化方式を SDK のスコープに含めるか、アプリが実装する前提のインタフェースのみを提供するかを決める
- SFrame を利用する場合に Sora サーバとの SDP ネゴシエーションが必要かを確認する

## 完了条件

- 送信側・受信側の両方で、エンコード後の音声・映像フレームをアプリが加工できること
- E2EE を有効にした状態で Sora へ接続し、音声・映像を送受信できること
- `CHANGES.md` の `## develop` に `[ADD]` エントリを追記していること

## Pending 理由

- sora-oss-private の issue で「不急」ラベルが付いており優先度が低い
- 「livekit が E2EE 頑張っててこの辺しっかりやってるが、時雨堂は一旦頑張らない」と方針が決まっている
- SDK がどこまで E2EE の仕組み（鍵管理・SFrame の暗号化方式）を提供するか、API を設けずアプリに委ねるかの設計判断が必要
- 現状でもアプリが `webrtc::RtpSenderInterface::SetFrameEncryptor` / `webrtc::RtpReceiverInterface::SetFrameDecryptor` を直接呼べば実現できるため、SDK としての優先度が低い

## Pending 解除条件

- 実装方針（SDK が提供する API の範囲と E2EE の鍵管理・暗号化の責務分担）が確定したこと
- E2EE を有効にした状態で Sora と音声・映像を送受信できる見込みが立ったこと
