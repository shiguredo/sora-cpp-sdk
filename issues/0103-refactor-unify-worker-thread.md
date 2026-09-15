# SoraClientContext の worker thread を network thread に統一する

- Created: 2026-09-15
- Completed: {YYYY-MM-DD}
- Branch: feature/refactor-unify-worker-thread
- Polished: 2026-09-15

## 目的

libwebrtc の issue 558821261「Deprecate and remove PeerConnectionFactoryDependencies::worker_thread」で worker thread が廃止される。CL 501620「Default worker thread to network thread」はマージ済みで、CL 502480「Warn when a distinct worker thread is configured」もマージ済みのため、`worker_thread != nullptr && worker_thread != network_thread_` のとき DEPRECATION ログが出力される。削除系 CL (499302 / 501640 / 501720 / 502000 / 502500 / 502860 / 502940 / 502960) はレビュー中で、`PeerConnectionFactoryDependencies::worker_thread` と `PeerConnectionFactoryInterface::worker_thread()` は将来削除される。

Sora C++ SDK は network thread とは別の worker thread を生成して `dependencies.worker_thread` に渡しているため、CL 502480 を含む libwebrtc（webrtc-build を更新した時点から）では DEPRECATION ログが出力され続け、削除系 CL が取り込まれた libwebrtc に上げるとビルドできなくなる。まず本 issue で専用 worker thread の生成をやめて network thread を使うようにする。worker_thread の利用箇所と API を全て無くす対応は、558821261 を実装した libwebrtc をマージした後に別 issue で行う。

## 現状

- `include/sora/sora_client_context.h` の `SoraClientContext` は `network_thread()` / `worker_thread()` / `signaling_thread()` の 3 つのアクセサを公開し、`std::unique_ptr<webrtc::Thread>` の `network_thread_` / `worker_thread_` / `signaling_thread_` を保持している
- `src/sora_client_context.cpp` の `SoraClientContext::Create` は network (`webrtc::Thread::CreateWithSocketServer()`)、worker (`webrtc::Thread::Create()`)、signaling (`webrtc::Thread::Create()`) の 3 スレッドを生成して `Start()` し、`dependencies.network_thread` / `dependencies.worker_thread` / `dependencies.signaling_thread` に設定している。`dependencies.worker_thread` には network thread とは別のスレッドを渡しているため、CL 502480 を含む libwebrtc では DEPRECATION ログが出力される
- `worker_thread_` への `BlockingCall` は `SoraClientContext::Create` に 6 箇所、`~SoraClientContext` に 1 箇所の計 7 箇所あり、worker thread 上で次の処理を行っている
  - ADM の生成 (`sora::CreateAudioDeviceModule`)
  - `configure_dependencies` 後の `dependencies.adm` の再取得
  - `VideoCodecFactory` の生成失敗時、PeerConnectionFactory の生成失敗時、オーディオデバイス設定失敗時の `adm = nullptr` による ADM の破棄 (3 箇所)
  - `adm->Init()` と `ConnectionContext::MediaEngineReference` の生成
  - 録音 / 再生デバイスの列挙 (`RecordingDevices` / `PlayoutDevices` / `RecordingDeviceName` / `PlayoutDeviceName`) と既定デバイスの設定 (`SetRecordingDevice` / `SetPlayoutDevice`)
  - 指定デバイスの `InitMicrophone()` / `InitSpeaker()`
  - デストラクタでの `ConnectionContext::MediaEngineReference` の破棄
- `media_engine()` と `ConnectionContext::MediaEngineReference` は worker thread 上でのみ生成・破棄・アクセスが許されるため、これらの処理は worker thread 上で実行している。`IsCurrent()` による分岐はデストラクタの 1 箇所にある
- `src/sora_peer_connection_factory.cpp` の `CreateModularPeerConnectionFactoryWithContext` は `webrtc::PeerConnectionFactoryProxy::Create(factory->signaling_thread(), factory->worker_thread(), factory)` を呼んでいる
- 専用 worker thread の生成をやめて `dependencies.worker_thread` を設定しない場合、`dependencies.worker_thread` が nullptr のままになる。`SoraClientContextConfig::configure_dependencies` を設定する利用側 (sora-unity-sdk / sora-python-sdk / zakuro) は `dependencies.worker_thread` を参照しているため、nullptr 参照でクラッシュする

## 設計方針

- 専用 worker thread の生成 / `Start()` / `Stop()` をやめ、factory の worker thread として network thread を使う
- 方針1 では `dependencies.worker_thread` に network thread を渡す。`worker_thread` を未設定にすると `dependencies.worker_thread` が nullptr のままになり、`SoraClientContextConfig::configure_dependencies` を設定する利用側 (sora-unity-sdk / sora-python-sdk / zakuro) が参照してクラッシュするため
- worker thread 上で実行していた処理を network thread に移す。`worker_thread_` への `BlockingCall` とデストラクタの `IsCurrent()` を network thread に置き換える
- `SoraClientContext::worker_thread()` アクセサは方針2 まで残す。利用側 (sora-unity-sdk / sora-python-sdk / zakuro) が方針2 で追随するまで削除すると、利用側がビルドできなくなるため。アクセサが参照する `worker_thread_` メンバは network thread を指すようにする
- デストラクタでの `ConnectionContext::MediaEngineReference` の破棄も network thread 上で行う

## 完了条件

- network thread とは別の専用 worker thread が生成されなくなっていること (`worker_thread_` の `webrtc::Thread::Create()` と `Start()` / `Stop()` がなくなる)
- `dependencies.worker_thread` に network thread が設定されていること
- ADM の生成 / 破棄、`adm->Init()`、`ConnectionContext::MediaEngineReference` の生成 / 破棄、オーディオデバイスの列挙と設定が network thread 上で実行されること
- `SoraClientContext::worker_thread()` アクセサが残り、network thread を返すこと
- 専用 worker thread を設定していることを知らせる DEPRECATION ログが出力されなくなること
- 既存のテストが通ること
- `CHANGES.md` の `## develop` に変更内容を追記していること (機能に直接影響しない変更のため `### misc`)
