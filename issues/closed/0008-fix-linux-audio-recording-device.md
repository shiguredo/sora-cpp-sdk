# Linux で `--audio-recording-device` / `--audio-playout-device` を指定しても正しく音声デバイスが認識されない

- Priority: High
- Created: 2026-06-16
- Completed: 2026-06-16
- Model: Kimi Code CLI
- Branch: feature/fix-linux-audio-recording-device
- Polished: 2026-06-16

## 目的

`SoraClientContextConfig::audio_recording_device` / `audio_playout_device`（sumomo では `--audio-recording-device` / `--audio-playout-device`）にデバイス名を指定した場合、Linux（PulseAudio/ALSA）環境で ADM の初期化順序と `PeerConnectionFactory` 作成時の上書きにより、指定デバイスが正しく認識・選択されない問題を修正する。

本問題は Linux で顕在化したが、修正は `SoraClientContext::Create()` 内の共通処理として Android 以外の全プラットフォームに適用する。

## 現状

### 根本原因

`SoraClientContext::Create()` 内の ADM 初期化・デバイス列挙・Factory 作成の順序に問題があった。

1. `adm->Init()` より前に `RecordingDevices()` / `PlayoutDevices()` / `RecordingIsAvailable()` / `PlayoutIsAvailable()` を呼んでいた。Linux の PulseAudio/ALSA 実装では mainloop/context が未初期化の状態でこれらを呼ぶと失敗する。
2. `PeerConnectionFactory` 作成時に `WebRtcVoiceEngine::Init()` → `adm_helpers::Init()` が呼ばれ、`SetRecordingDevice()` / `SetPlayoutDevice()` / `InitMicrophone()` / `InitSpeaker()` でデバイス設定がデフォルトデバイスに上書きされる。Windows では `kDefaultCommunicationDevice` / `kDefaultDevice`、Linux / macOS では index 0 が使われる。

## 設計方針

`SoraClientContext::Create()` 内で ADM の初期化・デバイス列挙・Factory 作成後の再設定を整理する。対象は Android 以外の全プラットフォームで共通に行う。

- `get_audio_devices()` 実行前に `adm->Init()` を呼び、Linux ADM で mainloop/context を初期化する。`adm->Init()` の失敗時は `Create()` が `nullptr` を返す。
- `RecordingIsAvailable()` / `PlayoutIsAvailable()` は列挙前の全体可用性チェックとして呼び、失敗してもデバイス列挙を継続する。
- デバイス名は `RecordingDeviceName()` / `PlayoutDeviceName()` で取得した名前または GUID の完全一致（大小文字区別あり）でマッチングする。`audio_recording_device` / `audio_playout_device` は `std::optional<std::string>` 型であり、`std::nullopt` の場合のみ未指定とする。空文字列も存在しないデバイス名として扱われ `Create()` が失敗する。
- `PeerConnectionFactory` 作成後に `set_audio_device()` で指定デバイスを再設定する。`SetRecordingDevice()` / `SetPlayoutDevice()` の失敗は `Create()` を失敗させる。
- 指定デバイス名が設定されていて、かつ対応するデバイス一覧が空でない場合に `InitMicrophone()` / `InitSpeaker()` を呼び直す。これらの失敗は WARNING ログを出力して接続を継続する。
- デバイス名未指定時は、デバイス一覧が空でない場合に `SetRecordingDevice(0)` / `SetPlayoutDevice(0)` を明示的に呼ぶ。失敗は無視して接続を継続する。これは Windows で無効なデバイス index (-1) が選ばれてしまうのを防ぐためである。
- `adm_helpers::Init()` 内での stereo 再設定は本 issue では対象外とする。
- `configure_dependencies` コールバックで ADM が差し替えられた場合、`dependencies.adm` を再取得した上で同様の初期化・デバイス設定を適用する。`dependencies.adm` が `nullptr` の場合は `Create()` を失敗させる。
- Android ではデバイスの数が 1 個として返される上に、`RecordingDeviceName()` / `PlayoutDeviceName()` を呼び出すとクラッシュする既知の制約があるため、オーディオデバイスの列挙と設定をプリプロセス分岐でスキップする。iOS についても同様のクラッシュリスクがあり、現状は未検証。問題が発現した場合は別途対応する。

## 後方互換への影響

- デバイス名未指定時: Windows で無効なデバイス index (-1) が選ばれていた可能性があるが、本修正でデバイス一覧が空でない場合は index 0 が明示的に設定される。これは既存の挙動を正しい方向に矯正するものであり、利用者への破壊的変更ではない。
- Android: オーディオデバイスの列挙と設定をスキップするため、既存の挙動に変更はない。
- `configure_dependencies` でカスタム ADM を利用する場合: `adm->Init()` / `RecordingDevices()` / `PlayoutDevices()` / `RecordingIsAvailable()` / `PlayoutIsAvailable()` / `SetRecordingDevice()` / `SetPlayoutDevice()` / `RecordingDeviceName()` / `PlayoutDeviceName()` / `InitMicrophone()` / `InitSpeaker()` をサポートする必要がある。これらの API 呼び出しは必ず発生し、失敗時には `Create()` が失敗する。また、カスタム ADM は `adm_helpers::Init()` からの 2 回目の `Init()` 呼び出しも許容する必要がある。
- `use_audio_device = false`（ダミー ADM）時: ダミー ADM の実装に依存するが、本修正による実害は想定しない。ただし、`RecordingIsAvailable()` / `PlayoutIsAvailable()` が失敗し WARNING ログが出力される可能性がある。
- 存在しないデバイス名を指定した場合: 従来は `adm_helpers::Init()` によるデフォルトデバイス設定で接続が続行していた可能性があるが、本修正後は `Create()` が失敗する。これはバグ修正に伴う正当な挙動変更である。
- iOS では `AudioOutputHelper` 経由で音声出力先を制御する世界観であり、本修正の ADM デバイス設定が実用上どう影響するかは未検証。問題が発現した場合は別途対応する。

## 完了条件

- Linux（PulseAudio/ALSA）環境で `--audio-recording-device` / `--audio-playout-device` に実在するデバイス名を指定した場合、`SoraClientContext::Create()` が成功し、sumomo の接続が続行されること。
- Windows / macOS でデバイス名未指定時・指定時の接続が従来通り動作すること。
- 存在しないデバイス名を指定した場合に `SoraClientContext::Create()` が失敗すること。
- デバイス名未指定時、デバイス一覧が空でない場合は index 0 が設定され、接続が継続すること。
- Android ではオーディオデバイスの列挙と設定を行わず、既存の挙動が維持されること。
- `use_audio_device = false`（ダミー ADM）時に `SoraClientContext::Create()` が従来通り成功すること。
- `CHANGES.md` の `## develop` に `[FIX]` エントリを `shiguredo-changelog` スキルの種別順規約に従って追記すること。
- `set_audio_device()` のマッチングロジックを固定のダミーデータで検証する単体テストを追加、または既存テストでカバーすること。

## 解決方法

`src/sora_client_context.cpp` の `SoraClientContext::Create()` を修正した。

### 行った変更

- `set_audio_device()` / `get_audio_devices()` / `reconfigure_audio_device()` のスコープを整理し、Factory 作成前後の両方でデバイス設定・再設定が行えるようにした。
- `worker_thread_->BlockingCall` 内で `adm->Init()` を呼び出してから `get_audio_devices()` を実行するようにした。`adm->Init()` の失敗時は `Create()` が `nullptr` を返す。
- `get_audio_devices()` 内で `RecordingDevices()` / `PlayoutDevices()` でデバイス数を取得し、`RecordingDeviceName()` / `PlayoutDeviceName()` で名前を列挙するようにした。`RecordingDevices()` / `PlayoutDevices()` が負の値を返した場合は空のデバイス一覧として扱う。
- `RecordingIsAvailable()` / `PlayoutIsAvailable()` の失敗は警告ログまたは情報ログを出力して列挙を継続する。
- `PeerConnectionFactory` 作成後に `reconfigure_audio_device()` を実行し、`set_audio_device()` で指定デバイスを再設定する。
- `reconfigure_audio_device()` 内で、指定デバイス名が設定されていて対応するデバイス一覧が空でない場合に `InitMicrophone()` / `InitSpeaker()` を呼び直す。失敗しても WARNING ログを出力して処理を継続する。
- Android では `RecordingDeviceName()` / `PlayoutDeviceName()` を呼ぶとクラッシュする既知の制約があるため、オーディオデバイスの列挙と設定をプリプロセス分岐でスキップする。

### 確認結果

- Linux 実環境（PulseAudio/ALSA）で `--audio-recording-device <デバイス名>` / `--audio-playout-device <デバイス名>` を指定した sumomo が接続に成功したことを確認した。
