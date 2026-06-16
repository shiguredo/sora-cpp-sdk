# Linux で `--audio-recording-device` / `--audio-playout-device` を指定しても正しく音声デバイスが認識されない

## 背景

`sora_client_context.cpp` の `SoraClientContext::Create()` 内で、`--audio-recording-device` や `--audio-playout-device` に指定した名前から ADM のデバイスインデックスを特定し、`SetRecordingDevice()` / `SetPlayoutDevice()` を呼んでいる。

## 問題

Linux 環境で `--audio-recording-device` や `--audio-playout-device` にデバイス名を指定して sumomo などを実行すると、指定したデバイスが認識されず接続に失敗する。

## 再現手順

1. Linux（PulseAudio/ALSA）環境で複数の音声デバイスが認識されている状態にする
2. sumomo などで `--audio-recording-device デバイス名` を指定して実行する
3. 以下のようなログが出力され、接続に失敗する

```
(sora_client_context.cpp:152): Failed to RecordingIsAvailable
(sora_client_context.cpp:152): Failed to PlayoutIsAvailable
(sora_client_context.cpp:212): No recording device found: name=デバイス名
```

## 期待する動作

指定した音声デバイスが正しく認識・選択され、接続が続行される。

## 実際の動作

デバイス列挙に失敗し、指定したデバイスが見つからないとして接続に失敗する。
`--list-devices` ではデバイス一覧は取得できる。

## 原因

1. `SoraClientContext::Create()` 内で `adm->Init()` を呼んでいない
   - `DeviceList::EnumAudioRecording()` では `adm->Init()` 後にデバイス列挙を行っているが、`SoraClientContext::Create()` では `adm->Init()` なしに `RecordingDevices()` / `RecordingIsAvailable()` を呼んでいる
   - Linux の PulseAudio/ALSA 実装では `Init()` 前にこれらを呼ぶと mainloop/context が未初期化のため失敗する

2. `PeerConnectionFactory` 作成時に `adm_helpers::Init()` でデバイス設定が上書きされる
   - `WebRtcVoiceEngine::Init()` → `adm_helpers::Init()` の中で `SetRecordingDevice(0)` / `SetPlayoutDevice(0)` が呼ばれる
   - そのため、事前に指定デバイスを設定していても、後からデフォルトデバイス（index 0）に戻されてしまう

## 修正案

`src/sora_client_context.cpp` にて以下を行う。

- `get_audio_devices()` 実行前に `adm->Init()` を呼ぶ
- `RecordingIsAvailable()` / `PlayoutIsAvailable()` が失敗しても、デバイス名の列挙は継続する
- `PeerConnectionFactory` 作成後に、もう一度 `set_audio_device()` を呼び、指定デバイスを設定し直す
- 同時に `InitMicrophone()` / `InitSpeaker()` も指定デバイスに対して呼び直す
