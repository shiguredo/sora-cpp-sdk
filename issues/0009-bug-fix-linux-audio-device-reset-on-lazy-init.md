# Linux で指定オーディオデバイスが PeerConnection 作成時の遅延 Init でデフォルトに戻る問題を修正する

- Priority: High
- Created: 2026-06-22
- Completed: {YYYY-MM-DD}
- Model: Kimi Code CLI
- Branch: feature/change-linux-audio-device-reset-on-lazy-init
- Polished: 2026-06-22
- Reporter: @voluntas

## 目的

`SoraClientContextConfig::audio_recording_device` / `audio_playout_device` (sumomo の `--audio-recording-device` / `--audio-playout-device`) に実在するデバイス名を指定した場合、指定デバイスが上書きされてしまう問題を修正する。

問題の原因は、 `WebRtcVoiceEngine::Init()` / `adm_helpers::Init()` が `PeerConnectionFactory` 作成時ではなく、最初の `PeerConnection` 作成時まで遅延されることにある。

## 優先度根拠

ユーザーからの動作確認フィードバックであり、PC のデフォルトオーディオデバイスと異なるデバイスを指定した場合に音声疎通ができない。過去に `PeerConnectionFactory` 作成直後の再設定を追加したが、実際の `WebRtcVoiceEngine::Init()` は `PeerConnection` 作成時まで遅延しており、それをカバーできていない。

## 現状

`src/sora_client_context.cpp` の `SoraClientContext::Create()` 内では、 `PeerConnectionFactory` 作成後、 `#else` ブロック内の worker thread 上で `adm->Init()` とデバイス列挙・設定、さらに `reconfigure_audio_device` による二回目の設定を行っている。これらは `PeerConnectionFactory` 作成時に呼ばれる `WebRtcVoiceEngine::Init()` → `adm_helpers::Init()` による上書きを防ぐことを意図していた。

しかし、現行の libwebrtc では `WebRtcVoiceEngine::Init()` / `adm_helpers::Init()` は `PeerConnectionFactory` 作成時ではなく、最初の `PeerConnection` 作成時に `ConnectionContext::MediaEngineReference` の構築によって呼ばれる。 `PeerConnectionFactory::CreateAudioSource()` は `LocalAudioSource::Create()` を呼ぶだけで、ボイスエンジンの Init は発生しない。そのため、 `SoraClientContext::Create()` 内のデバイス設定はすべて Init より前に実行され、後から `SetRecordingDevice(0)` / `SetPlayoutDevice(0)` でデフォルトデバイスに上書きされてしまう。

ユーザーから提供されたログでは、指定デバイスに対する `SetRecordingDevice(4)` の成功後、 `WebRtcVoiceEngine::Init` の直後に `SetRecordingDevice(0)` が実行され、指定デバイスが上書きされている。

```
(audio_device_impl.cc): SetRecordingDevice(4)
(sora_client_context.cpp): Succeeded SetRecordingDevice: index=4 ...
...
(webrtc_voice_engine.cc): WebRtcVoiceEngine::Init
(audio_device_impl.cc): SetPlayoutDevice(0)
(audio_device_impl.cc): SetRecordingDevice(0)
```

## 設計方針

`SoraClientContext` に `webrtc::ConnectionContext::MediaEngineReference` を保持し、 `SoraClientContext::Create()` 内の非 Android / iOS パスで `WebRtcVoiceEngine::Init()` を同期的に発生させた上で、ユーザー指定デバイスを改めて設定する。

- `include/sora/sora_client_context.h` に `std::unique_ptr<webrtc::ConnectionContext::MediaEngineReference> media_engine_ref_` メンバーを追加する。 `connection_context_` の直後、つまりメンバー宣言の最後に追加する。デストラクタで明示的に worker thread 上で先に解放するため、RAII の自動破棄順序には頼らない。
- Android / iOS では `media_engine_ref_` を作成せず、デバイス列挙・設定も従来通り行わない。音声デバイス選択が機能しないプラットフォームであり、本問題の影響を受けないため。
- 非 Android / iOS では、 `src/sora_client_context.cpp` の `#else` ブロック内で worker thread 上に `MediaEngineReference` を作成する。 `ConnectionContext::AddRefMediaEngine()` / `ReleaseMediaEngine()` は worker thread 限定なので、作成・破棄の両方を `worker_thread_->BlockingCall()` で行う必要がある。 `connection_context_->is_configured_for_media()` が false の場合は音声メディアが無効化されているため、 `MediaEngineReference` を作成しない。
- `media_engine_ref_` は `SoraClientContext` の生存期間中保持し、メディアエンジンの `Terminate()` / 再 `Init()` が発生するのを防ぐ。これで最初の `PeerConnection` 作成時にもう一度 Init が走ってデバイスが上書きされることを回避する。
- `MediaEngineReference` 作成前に、同じ worker thread ブロック内で `adm->Init()` を呼び出し、失敗時は `adm` をクリアして `Create()` を `return nullptr` する。 `adm_helpers::Init()` 内では `adm->Init()` の戻り値を `RTC_CHECK_EQ(0, ...)` で検証しており、失敗時にプロセスが abort するため、事前に成功させておくことでそのリスクを低減する。
- `MediaEngineReference` 作成時の `adm_helpers::Init()` 内で二回目の `adm->Init()` が呼ばれるが、現状のコードでも `SoraClientContext::Create()` 内の `adm->Init()` と `PeerConnection` 作成時の `adm_helpers::Init()` という二重呼び出しが発生しており、ユーザー環境では後者が成功している。本方式はこの二回目の呼び出しを `SoraClientContext::Create()` 内に前倒しするだけである。
- `media_engine_ref_` 作成後に ADM のデバイス列挙と `SetRecordingDevice()` / `SetPlayoutDevice()` を行う。 `SetRecordingDevice()` / `SetPlayoutDevice()` が成功し、かつ `config_.audio_recording_device` / `config_.audio_playout_device` が設定済みでデバイス一覧が空でない場合に、 `InitMicrophone()` / `InitSpeaker()` を呼ぶ。既存の `reconfigure_audio_device` に相当する二回目の設定は削除する。
- `use_audio_device = false`（ダミー ADM）時は非 Android / iOS でも `#else` ブロックに入る。 `MediaEngineReference` を作成してよく、 `adm_helpers::Init()` はダミー ADM 上で実行され、デバイス設定は no-op となる。
- `media_engine_ref_` の破棄は `~SoraClientContext()` 内で `worker_thread_->BlockingCall([&] { media_engine_ref_.reset(); })` として、worker thread 停止前に行う。
- `SoraClientContext` の ABI が変更されるため、SDK 利用者はヘッダとライブラリを同時に更新する必要がある。

sendonly / sendrecv / recvonly を問わず、最初の `PeerConnection` 作成前に Init が発生するため、非 Android / iOS ではすべてのロールをカバーする。

## 完了条件

- Linux 環境で `--audio-recording-device` / `--audio-playout-device` に実在するデバイス名を指定した場合、 `SoraClientContext::Create()` 完了時点で `SetRecordingDevice()` / `SetPlayoutDevice()` が指定デバイスに設定されること。
- その後 `PeerConnection` を作成しても `WebRtcVoiceEngine::Init()` によりデフォルトに戻らないこと。
- `SetRecordingDevice()` / `SetPlayoutDevice()` 後に `InitMicrophone()` / `InitSpeaker()` を呼んだ上で、sumomo の音声疎通が正常に行われること。
- デバイス名未指定、存在しないデバイス名指定時の既存挙動が退化しないこと。
- `MediaEngineReference` の作成・破棄が worker thread 上で行われていること。
- `MediaEngineReference` 作成前の `adm->Init()` 失敗時に `SoraClientContext::Create()` が `nullptr` を返し、プロセスが abort しないこと。
- Android / iOS では `media_engine_ref_` が作成されず、既存挙動に影響しないこと。
- `use_audio_device = false`（ダミー ADM）時に `SoraClientContext::Create()` が従来通り成功すること。
- `CHANGES.md` の `## develop` の既存 `[FIX] Linux で --audio-recording-device ...` エントリから、本対応で陳腐化する「Factory 作成後の二回目設定」「 `adm->Init()` 事前呼び出し」に関する記述を削除する。既存エントリ内の「 `PeerConnectionFactory` 作成時の `adm_helpers::Init()` で上書きされていた」という記述も、実際には最初の `PeerConnection` 作成時に上書きされるため、修正または削除する。既存エントリ内の他の修正（空文字列デバイス名時の `Create()` 失敗、 `use_audio_device = false` 時の WARNING 抑制、 iOS の `RecordingDeviceName()` / `PlayoutDeviceName()` 抑制）は維持し、新しい `[CHANGE]` エントリには本対応の内容のみ記載する。種別順規約（CHANGE → ADD → UPDATE → FIX）に従って `[CHANGE]` エントリを `## develop` の先頭に追加すること。

## 解決方法

1. `include/sora/sora_client_context.h` に `std::unique_ptr<webrtc::ConnectionContext::MediaEngineReference> media_engine_ref_` メンバーを追加する。 `connection_context_` の直後、メンバー宣言の最後に追加する。
2. `src/sora_client_context.cpp` のデストラクタを次のように変更する。
   ```cpp
   SoraClientContext::~SoraClientContext() {
     config_ = SoraClientContextConfig();
     worker_thread_->BlockingCall([&] { media_engine_ref_.reset(); });
     connection_context_ = nullptr;
     factory_ = nullptr;
     network_thread_->Stop();
     worker_thread_->Stop();
     signaling_thread_->Stop();
   }
   ```
3. `src/sora_client_context.cpp` の `SoraClientContext::Create()` で、 `factory_` 作成後の `#else` ブロック内を次のように変更する。
   - worker thread 上で `adm->Init()` を実行し、失敗時は `adm` と `dependencies.adm` の両方を `nullptr` にして `false` を返す。 ADM の解放を worker thread 上で行うためである。
   - 同じく worker thread 上で `MediaEngineReference` を作成する。 `connection_context_->is_configured_for_media()` が false の場合は音声メディアが無効化されているため、作成しない。
   - `MediaEngineReference` 作成後にデバイス列挙、デバイス設定、 `InitMicrophone()` / `InitSpeaker()` を行う。
   ```cpp
   auto success = c->worker_thread_->BlockingCall([&]() -> bool {
     if (adm->Init() != 0) {
       RTC_LOG(LS_ERROR) << "Failed to initialize ADM";
       adm = nullptr;
       dependencies.adm = nullptr;
       return false;
     }

     if (c->connection_context_->is_configured_for_media()) {
       c->media_engine_ref_ =
           std::make_unique<webrtc::ConnectionContext::MediaEngineReference>(
               c->connection_context_);
     }

     // 以下、デバイス列挙・設定・InitMicrophone() / InitSpeaker()
     return true;
   });
   if (!success) {
     return nullptr;
   }
   ```
4. `src/sora_client_context.cpp` 内の「 `PeerConnectionFactory` 作成時に `WebRtcVoiceEngine::Init()` が呼ばれる」というコメントを、次のような内容に更新する。
   ```cpp
   // WebRtcVoiceEngine::Init() / adm_helpers::Init() は PeerConnectionFactory
   // 作成時ではなく、最初の PeerConnection 作成時まで遅延される。
   // ここで ConnectionContext::MediaEngineReference を作成して強制的に Init を
   // 完了させ、その後に ADM のデバイス設定を行う。
   ```
5. `CHANGES.md` の `## develop` 先頭に、例えば次のような `[CHANGE]` エントリを追加する。既存の同問題に関する `[FIX] Linux で --audio-recording-device ...` エントリから、本対応で陳腐化する「Factory 作成後の二回目設定」「 `adm->Init()` 事前呼び出し」に関する記述を削除する。既存エントリ内の「 `PeerConnectionFactory` 作成時の `adm_helpers::Init()` で上書きされていた」という記述も、実際には最初の `PeerConnection` 作成時に上書きされるため、修正または削除する。既存エントリ内の他の修正は維持する。
   ```md
   - [CHANGE] `SoraClientContext::Create()` 内で `ConnectionContext::MediaEngineReference` を保持し、
       `PeerConnection` 作成時の遅延 Init により指定オーディオデバイスがデフォルトに戻っていた問題を修正する
       - `include/sora/sora_client_context.h` に `media_engine_ref_` メンバーを追加し ABI を変更する
       - 非 Android / iOS では `SoraClientContext::Create()` 完了時に `WebRtcVoiceEngine::Init()` を発生させ、指定デバイス設定後に `InitMicrophone()` / `InitSpeaker()` を行う
       - Android / iOS では `media_engine_ref_` を作成せず、既存挙動に影響しない
       - @voluntas
   ```
