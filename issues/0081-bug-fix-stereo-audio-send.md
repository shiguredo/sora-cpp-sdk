# ステレオ音声を送信できるようにする

- Created: 2026-09-11
- Completed: {YYYY-MM-DD}
- Branch: feature/fix-stereo-audio-send
- Polished: 2026-09-14

## 目的

ステレオマイクで入力した音声を左右のチャネルを保ったまま Sora へ送信できるようにする。現状は 2 チャネルで送信しても左右が同じ音になり、ステレオ送信ができていない。

## 現状

- `src/sora_client_context.cpp` の `SoraClientContext::Create` は `sora::CreateAudioDeviceModule` で ADM を生成して `dependencies.adm` に設定するが、ステレオ録音の設定をしていない
- `src/audio_device_module.cpp` の `CreateAudioDeviceModule` はプラットフォーム既定の ADM を返すだけで、チャネル数の設定をしていない
- `AudioSendStream::ConfigureStream` のログでは `num_channels: 2` かつ `parameters` に `stereo: 1` が含まれており、送信設定と SDP はステレオになっている
- 一方で AEC3 のログは `num capture channels: 1` であり、ADM の録音がモノラルになっている。2 チャネルで送信しても左右が同じ音になる
- `dependencies.adm->SetStereoRecording(1)` を設定しても効果がない。macOS の ADM (`audio_device_mac.cc`) は録音チャネル数の既定値が 1 (`N_REC_CHANNELS = 1`) であり、`SetStereoRecording(true)` で `_recChannels` を 2 にしても、`InitRecording` でデバイスの入力チャネル数が 2 未満の場合は `Stereo recording unavailable on this device` を出力して `SetRecordingChannels(1)` になる
- 上記の `StereoRecordingIsAvailable` はデバイス（`AudioMixerManagerMac`）の入力チャネル数が 2 の場合のみ true を返し、ログは出力しない
- `webrtc::AudioProcessing::Config` の `pipeline.multi_channel_capture` は既定値が `true` であり（AEC3 有効時のみマルチチャネル処理に適用される）、true を設定しても挙動は変わらない。APM 側はマルチチャネル入力を処理できるが、ADM からの入力がモノラルのため録音はモノラルになる
- 検証環境: sumomo (macOS)、FHD Camera Microphone を使用（この環境では録音が 1 チャネルになり、ステレオ送信の確認には入力チャネル数が 2 のデバイスが必要）

## 設計方針

- libwebrtc の ADM と AudioProcessing のステレオ録音処理を追い、`SetStereoRecording(1)` が効かない原因を特定する
- macOS / Windows / Linux でステレオ録音が有効になる条件を確認する。macOS は ADM (`audio_device_mac.cc`) の `StereoRecordingIsAvailable` と `SetStereoRecording` の判定条件を確認する
- 必要に応じて ADM の生成方法や AudioProcessing の設定を見直す
- ステレオ送信の確認は、左右差が分かる音声を Sora 経由で受信し、左右の diff で判定する。マイクロフォンの入力が 1 チャネルの場合はステレオ録音にならないため、確認には `StereoRecordingIsAvailable` が true になる入力チャネル数が 2 のデバイスを使用し、受信側はステレオ受信に対応したクライアントを利用する

## 完了条件

- ステレオマイクからの入力を左右のチャネルを保ったまま Sora へ送信できること
- 送信した音声を受信側で確認し、左右差が保たれていること
- モノラルマイクの既存挙動に回帰がないこと
