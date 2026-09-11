# ステレオ音声を送信できるようにする

- Created: 2026-09-11
- Completed: {YYYY-MM-DD}
- Branch: feature/fix-stereo-audio-send
- Polished: {YYYY-MM-DD}

## 目的

ステレオマイクで入力した音声を左右のチャネルを保ったまま Sora へ送信できるようにする。現状は 2 チャネルで送信しても左右が同じ音になり、ステレオ送信ができていない。

## 現状

- `src/sora_client_context.cpp` の `SoraClientContext::Create` は `sora::CreateAudioDeviceModule` で ADM を生成して `dependencies.adm` に設定するが、ステレオ録音の設定をしていない
- `src/audio_device_module.cpp` の `CreateAudioDeviceModule` はプラットフォーム既定の ADM を返すだけで、チャネル数の設定をしていない
- `AudioSendStream::ConfigureStream` のログでは `num_channels: 2` かつ `parameters` に `stereo: 1` が含まれており、送信設定と SDP はステレオになっている
- 一方で AEC3 のログは `num capture channels: 1` であり、ADM の録音がモノラルになっている。2 チャネルで送信しても左右が同じ音になる
- `dependencies.adm->SetStereoRecording(1)` を設定しても効果がない
- `webrtc::AudioProcessing::Config` の `pipeline.multi_channel_capture = true` を設定しても効果がない
- macOS の ADM では `StereoRecordingIsAvailable` のログが出るのが一部のマイクのみで、`SetStereoRecording(0)` により `SetRecordingChannels(1)` になっている
- 検証環境: sumomo (macOS)、FHD Camera Microphone を使用

## 設計方針

- libwebrtc の ADM と AudioProcessing のステレオ録音処理を追い、`SetStereoRecording(1)` が効かない原因を特定する
- macOS / Windows / Linux でステレオ録音が有効になる条件を確認する。macOS は ADM (`audio_device_mac.cc`) の `StereoRecordingIsAvailable` と `SetStereoRecording` の判定条件を確認する
- 必要に応じて ADM の生成方法や AudioProcessing の設定を見直す
- ステレオ送信の確認は、左右差が分かる音声を Sora 経由で受信し、左右の diff で判定する

## 完了条件

- ステレオマイクからの入力を左右のチャネルを保ったまま Sora へ送信できること
- 送信した音声を受信側で確認し、左右差が保たれていること
- モノラルマイクの既存挙動に回帰がないこと
