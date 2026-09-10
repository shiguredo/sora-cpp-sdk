# Opus のサンプルレートを指定できるようにする

- Created: 2026-09-10
- Completed: {YYYY-MM-DD}
- Branch: feature/add-opus-sample-rate
- Polished: {YYYY-MM-DD}

## 目的

Sora C++ SDK で Opus エンコーダーの入力サンプルレートを 48000 Hz 以外（例: 16000 Hz）に指定できるようにする。音声変換の検証でさまざまなサンプルレートを試せるようにするため。実装はめるぽん、検証はトリキジが担当する。

## 現状

- `src/sora_client_context.cpp` の `SoraClientContext::Create` は `webrtc::CreateBuiltinAudioEncoderFactory()` で音声エンコーダーファクトリを生成しており、Opus エンコーダーのサンプルレートは 48000 Hz 固定で指定手段がない
- libwebrtc の `AudioEncoderOpus::SdpToConfig` は SDP のクロックレートが 48000 以外の Opus を拒否するため、SDP の内容からサンプルレートを変えることはできない
- 一方で libwebrtc の `AudioEncoderOpusConfig::IsOk` は `sample_rate_hz` として 16000 と 48000 のみを許可しており、`AudioEncoderOpus::MakeAudioEncoder` に `AudioEncoderOpusConfig` を直接渡せば 16000 Hz のエンコーダーを生成できる
- `SoraClientContextConfig::configure_dependencies` で `dependencies.audio_encoder_factory` を差し替えればアプリ側で対応することは可能だが、SDK としての指定手段は提供していない
- Opus の RTP タイムスタンプは RFC 7587 により常に 48000 Hz で増加し、SDP の rtpmap も `opus/48000/2` のまま変わらない。変更できるのはエンコーダーへの入力サンプルレートのみ
- 過去に `SoraSignalingConfig::audio_opus_params_clock_rate` があったが、Sora 側の `clock_rate` 廃止に伴い削除済み
- sumomo の `BeepAudioSource` は 48000 Hz 固定で音声を生成している

## 設計方針

- `SoraClientContextConfig` に Opus の入力サンプルレートを指定する項目（例: `audio_opus_sample_rate`）を追加する
- デフォルトは 48000 Hz とし、未指定時は従来どおり `webrtc::CreateBuiltinAudioEncoderFactory()` を利用する
- 48000 Hz 以外が指定された場合は、`AudioEncoderOpus::SdpToConfig` で得た `AudioEncoderOpusConfig` の `sample_rate_hz` を指定値に上書きして `AudioEncoderOpus::MakeAudioEncoder` を呼び出す音声エンコーダーファクトリを SDK が生成する
- 指定可能な値は libwebrtc が許可する 16000 と 48000 のみとし、それ以外はエラーまたは警告とする
- WebRTC の音声送信パイプライン（ `AudioCodingModule::PreprocessToAddData` ）は入力フレームをエンコーダーの `SampleRateHz()` にリサンプリングするため、48000 Hz の音声トラックのままでも 16000 Hz のエンコーダーでエンコードできる見込みだが、実際の動作は検証で確認する

## 完了条件

- Opus の入力サンプルレートとして 16000 Hz を指定できること
- 未指定時は従来どおり 48000 Hz で動作し、後方互換性が保たれること
- 16000 Hz を指定した状態で Sora へ接続し、音声が正常に送受信できることを確認していること
- `CHANGES.md` の `## develop` に `[ADD]` エントリを追記していること

## Pending 理由

- sora-oss-private で「不急」ラベルが付いており優先度が低い
- API の置き場所（ `SoraClientContextConfig` に項目を追加するか、アプリ側の `configure_dependencies` 利用のままとするか）と、カスタム音声エンコーダーファクトリの実装方法の設計判断が必要
- 16000 Hz の Opus で Sora と音声を送受信できるかが未検証

## Pending 解除条件

- 実装方針（API の位置とカスタム音声エンコーダーファクトリの実装方法）が確定したこと
- 16000 Hz を指定して Sora と音声を送受信できる見込みが立ったこと
