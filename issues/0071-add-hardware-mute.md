# カメラ・マイクのハードウェアミュート（デバイスオフ/オン）に対応する

- Created: 2026-09-10
- Completed: YYYY-MM-DD
- Branch: feature/add-hardware-mute
- Polished: 2026-09-15

## 目的

Sora C++ SDK を利用するアプリで、接続を維持したままカメラ・マイクのデバイスをオフ/オンできるようにする。

- デバイスオフは、カメラ・マイクのハードウェア自体を掴まない状態にすることを指し、カメラのランプやマイクのインジケーターが消灯する
- デバイスオンは、解放したハードウェアを再度掴んで送信を再開することを指す
- トラックミュート (`VideoTrackInterface::set_enabled` / `AudioTrackInterface::set_enabled`) とは異なる

会議アプリなどで入室時にカメラ・マイクをオフにして接続し、必要になった時点でオンにするユースケースに対応するため。Sora iOS SDK と Sora Android SDK はハードウェアミュートに対応済みで、Sora Unity SDK は Sora C++ SDK を利用するため、C++ SDK 側にデバイス制御の API が無いと対応できない。

## 現状

### 映像

- `sora::CreateCameraDeviceCapturer` は生成時にデバイスを掴んでキャプチャを開始し、`webrtc::VideoTrackSourceInterface` を返す。生成後にデバイスを解放して再度掴む API は無い
- プラットフォームごとのキャプチャラに停止処理はあるが、SDK としての再開手段が無い
  - `MacCapturer::Stop()` は `RTCCameraVideoCapturer` の停止のみで、再開する API が無い
  - `AndroidCapturer::Stop()` は `stopCapture` 後に capturer と SurfaceTextureHelper を dispose するため再開できない
  - Windows の `DeviceVideoCapturer::Destroy()` は private で、`vcm_` を nullptr にするため再開できない
  - `V4L2VideoCapturer::StopCapture()` は protected で、外部から停止できない
- アプリができるのは `VideoTrackInterface::set_enabled(false)` によるソフトミュートだけで、デバイスは掴んだままになる

### 音声

- ADM は `SoraClientContext::Create` が内部で生成して `PeerConnectionFactory` に渡す。`SoraClientContextConfig::use_audio_device` が選べるのは、実デバイスを使うかダミー ADM (`webrtc::AudioDeviceModule::kDummyAudio`) を使うかのどちらかだけで、`false` にするとマイク音声そのものが使えなくなる。接続後のデバイス解放/再取得の切り替えはできない
- 接続中に録音を停止してデバイスを解放し、再開する API を C++ SDK は提供していない。`SoraClientContextConfig::configure_dependencies` で ADM を取得すれば `StartRecording` / `StopRecording` を直接呼べるが、録音の停止/再開がデバイスの解放/再取得として機能する保証は無い
- 現行の libwebrtc の C++ `webrtc::AudioDeviceModule` には `StartRecording` / `StopRecording` はあるが、pause / resume に相当する API は無い
- Sora iOS SDK / Sora Android SDK のハードウェアミュートは、webrtc-build のパッチで追加された `RTCAudioDeviceModule` (`pauseRecording` / `resumeRecording`) と `org.webrtc.audio.JavaAudioDeviceModule` (`pauseRecording` / `resumeRecording`) を利用している。どちらも upstream の webrtc には存在せず、webrtc-build 側で追加されている
- sora-cpp-sdk の Android ADM は `webrtc::CreateJavaAudioDeviceModule` を使うネイティブ実装のため、Java 側に追加された pause / resume をそのままは利用できない

## 設計方針

- 映像と音声それぞれに、接続中にデバイスを解放/再取得する API を追加する。API 名、公開方法 (キャプチャラの共通インターフェース、`SoraClientContext` 経由など)、同期/非同期の扱いは実装時に決める
- デバイスオフ状態から接続を始める方法も整理する。接続直後にアプリがオフ API を呼ぶ方式ではカメラのランプやマイクのインジケーターが一瞬点灯するため、初期状態からデバイスオフにできる設定 (初期ミュート) の要否を検討する
- 映像
  - キャプチャラを破棄せずに停止/再開できるようにする。macOS / iOS は `RTCCameraVideoCapturer` の stop と start、Android は `CameraVideoCapturer` の `stopCapture` と再度の `startCapture`、Windows は `VideoCaptureModule`、Linux は V4L2 の停止/再開を利用できるか確認する
  - `CreateCameraDeviceCapturer` の返り値が `webrtc::VideoTrackSourceInterface` であるため、具体型を保持する方法を整理する
- 音声
  - `webrtc::AudioDeviceModule` を SDK から制御する API を設ける
  - iOS は webrtc-build のパッチ (`ios_audio_pause_resume.patch`) が追加する `RTCAudioDeviceModule` の `pauseRecording` / `resumeRecording` と、同じパッチで C++ 側の `webrtc::ios_adm::AudioDeviceModuleIOS` に追加された `PauseRecording` / `ResumeRecording` のどちらを使うか調査する。C++ SDK の iOS ビルドは `api/audio/create_audio_device_module.h` の `webrtc::CreateAudioDeviceModule` を使い、`AudioDeviceModuleImpl` が内部で `ios_adm::AudioDeviceIOS` を生成する。パッチは `AudioDeviceIOS` にも `PauseRecording` / `ResumeRecording` を追加しているが `AudioDeviceModuleImpl` からは透過しないため、`AudioDeviceModuleIOS` を利用するには `sdk/objc/native/api/audio_device_module.h` の `CreateAudioDeviceModule(env, bypass_voice_processing)` への切り替えが必要になる。当該ヘッダは webrtc の内部 (`sdk/objc/native/src/audio/audio_device_module_ios.h`) であり、公開方法も含めて整理する
  - Android はネイティブ ADM の `StopRecording` / `StartRecording` でマイクを解放できるか、または `JavaAudioDeviceModule` の pause / resume を利用する方法を調査する。webrtc-build への追加パッチが必要になる可能性も含めて整理する
  - Windows / macOS / Ubuntu は、ADM の `StopRecording` / `StartRecording` でデバイスが解放され再開できるか確認する
- ソフトミュートとデバイスオフの役割分担を整理する。デバイスオフ中に送信を止めるか、黒フレームや無音を送るか、ミュート解除時に黒いフレームが残らないようにする方法も含めて決める
- 既存 API の互換性を壊さない

## 完了条件

- audio:true / video:true のシグナリングのまま、カメラ・マイクのデバイスを掴まずに接続できる
- 接続を維持したままカメラ・マイクをオンにでき、オフにできる。オフ時はハードウェアを解放し、カメラのランプ・マイクのインジケーターが消灯する
- オン → オフ → オンを繰り返せる
- Sora Unity SDK が対応する Windows / macOS / iOS / Android / Ubuntu で動作を確認する。確認できないプラットフォームがある場合は、その理由と未確認の範囲を issue に記録する
- sumomo などのサンプルで一連の操作を確認できるようにする
- 既存の接続、ソフトミュート、デバイス切り替えに回帰がない
- 変更履歴 (`CHANGES.md`) の `## develop` にエントリを追記する
