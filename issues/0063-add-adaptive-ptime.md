# adaptivePtime を SDK オプションで音声トラックに適用する

- Created: 2026-09-10
- Completed: {YYYY-MM-DD}
- Branch: feature/add-adaptive-ptime
- Polished: 2026-09-14

## 目的

音声送信の adaptivePtime（適応的パケット化時間）を SDK の設定で有効化できるようにする。

adaptivePtime は W3C の `RTCRtpEncodingParameters.adaptivePtime` に対応する音声向けパラメータで、libwebrtc では audio の voice engine (`media/engine/webrtc_voice_engine.cc`) だけが `webrtc::RtpEncodingParameters::adaptive_ptime` を参照する。映像側では参照されない。サイマルキャストの有無に関係なく、SDK の設定で音声トラックに適用できるようにする。

## 現状

- `include/sora/sora_signaling.h` の `SoraSignalingConfig` に adaptivePtime を設定する項目がない
- `src/sora_signaling.cpp` の `SoraSignaling::OnRead` の `type == "offer"` 分岐で、`offer_config_.simulcast` が true かつ offer に `encodings` がある場合のみ、offer の `encodings[]` の `adaptivePtime` を `webrtc::RtpEncodingParameters::adaptive_ptime` に変換している
- 変換した encodings は `SoraSignaling::SetEncodingParameters` で `video_mid_` の video sender に `RtpSenderInterface::SetParameters` で設定している
- libwebrtc で `adaptive_ptime` を参照するのは audio の voice engine だけなので、この設定は実効しない
- `audio_mid_` は保持しているが、audio sender の encoding パラメータを設定する経路がない

## 設計方針

- `SoraSignalingConfig` に `std::optional<bool> audio_adaptive_ptime;` を追加する。`degradation_preference` と同じく optional で未設定を表現する
- `audio_adaptive_ptime` が true の場合、`audio_mid_` から audio transceiver を引き、その sender の `RtpParameters::encodings` の各要素の `adaptive_ptime` を true にして `SetParameters` する。適用場所は `SetDegradationPreference` を呼んでいる 3 つの `SessionDescription::CreateAnswer` コールバック（WebSocket 経由の offer、WebSocket 経由の update / re-offer、DataChannel 経由の re-offer）すべてとし、それぞれ `SetDegradationPreference` と同じ並びに追加する
  - audio transceiver が見つからない場合（音声トラックが追加されていない場合など）は `SetDegradationPreference` と同様にエラーログを出力して何もしない
- offer の `encodings[].adaptivePtime` は video sender に設定しない。`SoraSignaling::OnRead` の offer 分岐の encodings 変換処理のうち、`adaptivePtime` の変換ブロック（`if (p.count("adaptivePtime") != 0) { params.adaptive_ptime = p["adaptivePtime"].as_bool(); }`）だけを削除する。`rid` / `maxBitrate` / `minBitrate` / `scaleResolutionDownBy` / `maxFramerate` / `active` / `scalabilityMode` / `scaleResolutionDownTo` などの他の変換は変更しない。これにより video sender の `adaptive_ptime` は既定値（false）のままになる
- `audio_adaptive_ptime` が未設定または false の場合は audio sender の `adaptive_ptime` を変更しない（後方互換）
- 変更履歴（`CHANGES.md`）の `## develop` に `[ADD]` エントリを追記する。担当者行は変更内容より 2 文字分インデントを下げる

  ```markdown
  - [ADD] adaptivePtime を SDK オプションで音声トラックに適用できるようにする
    - @<担当者>
  ```

## 完了条件

- `audio_adaptive_ptime` を true にすると audio sender の encoding の `adaptive_ptime` が true になること
- `audio_adaptive_ptime` が未設定 / false の場合は audio sender の `adaptive_ptime` が変更されず従来どおりの挙動になること
- video sender の encoding に `adaptive_ptime` が設定されないこと（既定値の false のままであること）
- `CHANGES.md` の `## develop` に `[ADD]` が追記されていること

## テスト方針

- audio sender の `sender->GetParameters()` の `encodings[].adaptive_ptime` が true になることを確認する
- video sender の `encodings[].adaptive_ptime` が設定されないことを確認する
- 既存の offer 変換・適用処理にユニットテストがないため、`test/e2e.cpp` と同様に実 Sora サーバへ接続する Catch2 テストで audio track を追加して接続し、audio sender と video sender の `RtpSenderInterface::GetParameters()` を確認する。`test/e2e.cpp` の既存ケースは `config.audio = false` のため、audio 送信の検証には audio track の追加が必要
