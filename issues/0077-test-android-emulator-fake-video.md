# Android の Emulator で test アプリを動かして fake 映像を送信できるか確認する

- Created: 2026-09-11
- Completed: YYYY-MM-DD
- Branch: feature/add-android-emulator-fake-video
- Polished: YYYY-MM-DD
- Reporter: @zztkm

## 目的

Android のエミュレータ上で `test/android` の test アプリを動かし、HWA を使わず SW エンコーダーで fake 映像を Sora へ送信できるかを確認する。実機を用意せずに Android の動作確認や CI でのテストができるかどうかの判断材料にする。

## 現状

- `test/android/app/src/main/cpp/native-lib.cpp` の `Java_jp_shiguredo_hello_MainActivity_run` は `SoraClientContext` を生成して `HelloSora::Run()` を呼び出す。映像入力は `test/hello.cpp` の `HelloSora::Run()` が生成する `sora::FakeVideoCapturer` であり、カメラは使わない
- `HelloSora::Run()` は `HelloSoraConfig` の `capture_width` / `capture_height` (既定 1024 x 768) で fake 映像を生成する
- Android の動画エンコーダーは `src/sora_video_codec_factory.cpp` の `CreateVideoCodecFactory` 内、`SORA_CPP_SDK_ANDROID` 分岐で `CreateAndroidVideoEncoderFactory(jni_env)` を生成する。`jni_env` が nullptr の場合は factory が nullptr になる
- そのためエミュレータで Android 用 HWA エンコーダーが利用できない場合、SW エンコーダーへフォールバックせず映像を送信できない
- `test/android/app/build.gradle` は `abiFilters 'arm64-v8a'` のみをビルドする。Apple Silicon のエミュレータは arm64-v8a を実行できるが、x86_64 のエミュレータを使う場合は ABI の対応が必要である
- `test/android/app/src/main/cpp/native-lib.cpp` の signaling URL と channel ID はプレースホルダ ("シグナリングURL" / "チャンネルID") のため、実際に送信するには設定が必要である
- 既存の GitHub Actions に Android エミュレータを起動するワークフローはない

## 設計方針

最初のステップとして、HWA を使わず SW で動くようにするために `src/sora_video_codec_factory.cpp` の `CreateVideoCodecFactory` 内の `SORA_CPP_SDK_ANDROID` 分岐を `webrtc::CreateBuiltinVideoEncoderFactory()` に差し替えた状態で、エミュレータでの送信を確認する。この時点では Android HWA を切り捨てて検証を優先する。

送信を確認できた後に、エミュレータや HWA 非対応環境を検出して SW エンコーダーを選ぶ方法、`CreateAndroidVideoEncoderFactory` を残したまま切り替える方法を検討する。fake 映像は既存の `sora::FakeVideoCapturer` をそのまま使い、エミュレータで HWA に依存しないことだけを変える。

## 完了条件

- Android エミュレータ上で test アプリが起動し、`FakeVideoCapturer` の fake 映像が Sora へ送信され、受信側で確認できること
- 送信できない場合は、どの段階 (エンコーダー初期化、JNI、ABI、ネットワーク) で失敗するかを特定し、次の対応を判断できる状態になっていること
- 確認に使ったエミュレータ (ABI、OS バージョン) と手順を issue に記録すること
