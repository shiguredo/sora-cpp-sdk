# SoraClientContext に libwebrtc のフィールドトライアルを指定できるようにする

- Created: 2026-09-16
- Completed: 2026-09-17
- Branch: feature/add-webrtc-field-trials
- Polished: 2026-09-16
- Reporter: @voluntas

## 目的

`SoraClientContextConfig` に libwebrtc のフィールドトライアル文字列を指定する項目を追加し、
sora-python-sdk を含む利用側から `WebRTC-Video-PerSsrcKeyframes` などのフィールドトライアルを
有効化できるようにする。

## 現状

### フィールドトライアルを指定する経路がない

`SoraClientContextConfig` には libwebrtc のフィールドトライアルを指定する項目がない。
`SoraClientContextConfig::configure_dependencies` で
`webrtc::PeerConnectionFactoryDependencies::env` に `webrtc::Environment` を設定しても、
`src/sora_peer_connection_factory.cpp` の `PeerConnectionFactoryWithContext` が
`webrtc::CreateEnvironment()` を呼び直しており、設定した `Environment` が使われない。
`ConnectionContext::Create` と `webrtc::PeerConnectionFactory` のコンストラクタに渡す
`Environment` もそれぞれ別に生成されている。

### WebRTC-Video-PerSsrcKeyframes の効果

libwebrtc m154.8037.1.1 には `WebRTC-Video-PerSsrcKeyframes` フィールドトライアルが存在する。
有効にすると以下が変わる。

- `video/encoder_rtcp_feedback.cc` の `EncoderRtcpFeedback` が PLI/FIR を SSRC 単位で処理し、
  PLI を受けた SSRC に対応するレイヤーだけキーフレームを生成する。
  キーフレームの最小送出間隔も SSRC ごとに独立する
- `video/video_stream_encoder.cc` の `VideoStreamEncoder::SendKeyFrame` がレイヤー単位の
  キーフレーム要求（`next_frame_types_` の更新）に対応しているため、単一エンコーダーで
  サイマルキャストを構成している場合でも、PLI を受けた SSRC のレイヤーだけを
  キーフレームにできる
- `media/engine/simulcast_encoder_adapter.cc` には「`WebRTC-Video-PerSsrcKeyframes` が
  有効な場合は `x-google-per-layer-pli` による separate encoders の強制をしない」旨の
  コメントがあるが、m154.8037.1.1 の分岐（`separate_encoders_needed`）には
  フィールドトライアルの参照がなく、SEA の挙動はフィールドトライアルでは変わらない

Sora 本体ではサイマルキャストの PLI を rid 単位で制御する対応が進行中である。
配信者側の SDK でこのフィールドトライアルを有効にすると、
Sora から rid 単位で届く PLI に対して要求されたレイヤーのキーフレームだけを生成できる。

### フィールドトライアルが VideoSendStreamImpl まで届く経路

libwebrtc のソースコードで以下の経路を確認済み。

- `api/field_trials.cc` の `FieldTrials::Create` が `WebRTC-Video-PerSsrcKeyframes/Enabled/` を
  パースし、 `api/field_trials_view.h` の `FieldTrialsView::IsEnabled` が true を返す。
  不正な文字列は `Parse` が false を返し `Create` が nullptr を返す
- `api/environment/environment_factory.cc` の `EnvironmentFactory::Set` で
  `webrtc::Environment` に field trials が保持される。 field trials が設定されている場合は
  `DeprecatedGlobalFieldTrials` は使われない
- `pc/peer_connection_factory.cc` の `PeerConnectionFactory` はコンストラクタで受け取った
  `Environment` を `env_` に保持する。 `AssembleEnvironment` が
  `webrtc::PeerConnectionFactoryDependencies::env` を取り出す
- `PeerConnectionFactory::CreatePeerConnectionOrError` の `EnvironmentFactory env_factory(env_)` で
  field trials が継承され、 `CreateCall_s` に渡る。
  `webrtc::PeerConnectionDependencies::trials` は設定していないため上書きされない
- `call/call.cc` の `Call::CreateVideoSendStream` が `VideoSendStreamImpl(env_, ...)` を生成する
- `video/video_send_stream_impl.cc` が
  `env_.field_trials().IsEnabled("WebRTC-Video-PerSsrcKeyframes")` を参照して
  `EncoderRtcpFeedback` の `per_layer_keyframes` を決める

## 設計方針

- `SoraClientContextConfig` に `std::string field_trials` を追加する。
  空文字の場合はフィールドトライアルを設定しない
- `src/sora_client_context.cpp` の `SoraClientContext::Create` で `config.field_trials` が
  空文字の場合と空文字以外の場合を分ける。
  `webrtc::FieldTrials::Create` は空文字でも nullptr ではなく空の `FieldTrials` を返すため、
  空文字の分岐は `Create` を呼ぶ前に行う
  - 空文字の場合: `webrtc::CreateEnvironment()` で既定の `webrtc::Environment` を生成する。
    `EnvironmentFactory::Set` には何も設定しない（`DeprecatedGlobalFieldTrials` による
    libwebrtc の既定動作を維持する）
  - 空文字以外の場合: `webrtc::FieldTrials::Create` で `FieldTrials` を生成し、
    `webrtc::EnvironmentFactory` で field trials 付きの `webrtc::Environment` を生成する。
    `Create` が nullptr を返した場合（空文字以外の不正な文字列）はエラーログを出力して
    nullptr を返す
  - 生成した `Environment` を `webrtc::PeerConnectionFactoryDependencies::env` に設定し、
    `configure_dependencies` を呼び出す前に完了させる
- `src/sora_peer_connection_factory.cpp` の `PeerConnectionFactoryWithContext` が
  `webrtc::PeerConnectionFactoryDependencies::env` を尊重するように修正する。
  `env` がある場合は `ConnectionContext::Create` と
  `webrtc::PeerConnectionFactory` のコンストラクタに同じ `Environment` を渡す
- ADM にも同じ `Environment` を渡す
- sora-python-sdk 側は `SoraFactory` で `SoraClientContextConfig::field_trials` を設定する対応を別途行う

## 完了条件

- `SoraClientContextConfig::field_trials` に `WebRTC-Video-PerSsrcKeyframes/Enabled/` を指定すると、
  `webrtc::PeerConnectionFactoryDependencies::env` のフィールドトライアルで有効になること
- `field_trials` に不正な文字列を指定すると `SoraClientContext::Create` が nullptr を返すこと
- `field_trials` が空文字の場合は libwebrtc の既定動作になること
- 既存の Sora C++ SDK の利用側で回帰がないこと

## テスト

- `test/sora_client_context.cpp` を新規追加する
  - `configure_dependencies` で `webrtc::PeerConnectionFactoryDependencies::env` を取得し、
    `field_trials` が有効になっていることを確認する
  - 不正な文字列を指定した場合に `SoraClientContext::Create` が nullptr を返すことを確認する
  - 空文字の場合に `SoraClientContext::Create` が成功することを確認する
- `test/CMakeLists.txt` と `run.py` にテストのビルド設定を追加する
- `VideoSendStreamImpl` まで field trials が届くことは libwebrtc 内部のため単体テストでは確認できない。
  実配信での確認は E2E テストまたは手動で別途行う

## 解決方法

`SoraClientContextConfig` に libwebrtc のフィールドトライアル文字列を指定できるようにした。

- `include/sora/sora_client_context.h` の `SoraClientContextConfig` に `std::string field_trials` を追加した
- `src/sora_client_context.cpp` の `SoraClientContext::Create` で、`field_trials` が空文字の場合は `webrtc::CreateEnvironment` で既定の `webrtc::Environment` を生成し、空文字以外の場合は `webrtc::FieldTrials::Create` でパースした上で `webrtc::EnvironmentFactory` でフィールドトライアル付きの `webrtc::Environment` を生成する。`webrtc::FieldTrials::Create` が nullptr を返した場合（不正な文字列）はエラーログを出力して `SoraClientContext::Create` が nullptr を返す
- 生成した `webrtc::Environment` を `webrtc::PeerConnectionFactoryDependencies::env` に設定し、ADM にも同じ `webrtc::Environment` を渡すようにした
- `src/sora_peer_connection_factory.cpp` の `PeerConnectionFactoryWithContext` が `webrtc::PeerConnectionFactoryDependencies::env` を尊重し、`webrtc::ConnectionContext::Create` と `webrtc::PeerConnectionFactory` のコンストラクタに同じ `webrtc::Environment` を渡すようにした
- libwebrtc を m154.8037.1.2 に上げた。`webrtc::FieldTrials::Create` は m154.8037.1.1 の prebuilt libwebrtc に `api:field_trials` が含まれておらずリンクできないため、webrtc-build 側で `api:field_trials` がビルド対象に含まれるようになった m154.8037.1.2 を利用する
- `test/sora_client_context.cpp` を追加し、`test/CMakeLists.txt` と `run.py` にテストのビルド・実行設定を追加した
- `CHANGES.md` の `## develop` に追記した

確認:

- `python3 run.py build --test --disable-cuda ubuntu-24.04_x86_64` が通ることを確認した（CUDA を有効にしたビルドは環境の CUDA 側の問題で通らないため `--disable-cuda` で確認した）
- `test/sora_client_context.cpp` の 4 ケース 14 アサーションが通過することを確認した
- `base_renderer` / `video_factory_data_race` / `audio_device` の既存テストが通過することを確認した
- `VideoSendStreamImpl` までフィールドトライアルが届くことの実配信での確認は未実施
