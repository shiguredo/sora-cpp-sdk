# sumomo に Y4M / WAV ファイルの読み込み機能を追加する

- Created: 2026-09-10
- Completed: {YYYY-MM-DD}
- Branch: feature/add-y4m-wav-file-input-to-sumomo
- Polished: {YYYY-MM-DD}

## 目的

カメラやマイクのない環境で、実際の映像・音声ファイルを使って sumomo の送信を検証できるようにする。zakuro が持つ Y4M (YUV4MPEG2) と WAV の読み込み機能を sumomo に移植し、`--fake-capture-device` のテストパターンやビープ音ではなく任意のファイルを配信できるようにする。

## 現状

- `examples/sumomo/src/sumomo.cpp` の `Sumomo::Run()` は、`config_.fake_capture_device` が true のとき映像に `sora::FakeVideoCapturer`、音声に `BeepAudioSource` を使う。どちらも実行時に生成するテストパターンとビープ音であり、外部ファイルを入力にする経路はない
- `SumomoConfig` と `main()` の CLI11 定義にファイル入力関連のオプションはなく、fake の指定は `--fake-capture-device` のみである
- `include/sora/capturer/fake_video_capturer.h` の `sora::FakeVideoCapturer` と `FakeVideoCapturerConfig` には、幅・高さ・fps と `on_tick` コールバックしかなく、ファイルからフレームを供給する仕組みはない
- sora-cpp-sdk の `include/` / `src/` / `examples/` に Y4M / WAV の読み込み実装はない
- zakuro にも生の YUV ファイルを読み込む実装はなく、YUV は Y4M (YUV4MPEG2) として読み込む
- `examples/sumomo/CMakeLists.txt` の `target_sources` は `src/sumomo.cpp` と `src/sdl_renderer.cpp` のみである
- `examples/sumomo/README.md` の「映像と音声のデバイスに関するオプション」にファイル入力の記載はない (`--fake-capture-device` 自体も未記載)
- `e2e-test/sumomo.py` の `Sumomo` クラスは `fake_capture_device=True` がデフォルトで、`--fake-capture-device` を使った E2E テストが既にある
- zakuro には以下がある
  - `src/y4m_reader.{h,cpp}` の `Y4MReader`: YUV4MPEG2 のヘッダを読み、指定ミリ秒時点のフレームを 4:2:0 の I420 フレーム全体として書き込む。`Ip` (progressive) と `C420*` のみ対応する。末尾まで読むと先頭へループする
  - `src/wav_reader.{h,cpp}` の `WavReader`: RIFF/WAVE の 16 bit PCM (モノラル / ステレオ) を `channels` / `sample_rate` / `std::vector<int16_t>` に読み込む
  - `src/fake_video_capturer.{h,cpp}` の `FakeVideoCapturer`: `FakeVideoCapturerConfig::Type::Y4MFile` のとき `Y4MReader` からフレームを取得し、`sora::ScalableVideoTrackSource::OnCapturedFrame` に渡す
  - `src/zakuro.cpp` の `VirtualClient` 設定: `--fake-video-capture` / `--fake-audio-capture` で指定されたファイルを読み込み、WAV は `fake_audio` (sample_rate / channels / data) として音声デバイスモジュールに渡す
  - `src/util.cpp`: `--fake-video-capture` / `--fake-audio-capture` を `CLI::ExistingFile` 付きで定義する

## 設計方針

- zakuro の `Y4MReader` / `WavReader` を sumomo 配下へ移植する。sumomo 固有の要件のため、SDK 本体 (`include/sora/` / `src/`) には追加しない方針を第一候補とする
- 映像は指定された Y4M ファイルを I420 に展開し、`sora::ScalableVideoTrackSource` から送出する。既存の `sora::FakeVideoCapturer` を拡張するか、sumomo 固有の capturer を追加するかは設計時に決める
- 音声は WAV の PCM を 10 ms 単位で `AudioSourceInterface` の sink に流す。既存の `BeepAudioSource` と同じ仕組みを利用する
- 次の点は設計時に決める
  - オプション体系: `--video-file` / `--audio-file` を追加するか、zakuro と同じ `--fake-video-capture` / `--fake-audio-capture` にするか、`--fake-capture-device` を拡張するか
  - WAV のサンプルレート・チャンネル数が 48 kHz / モノラル以外の場合の扱い (リサンプリングするか、そのまま sink に渡すか、エラーにするか)
  - Y4M の解像度と `--resolution` が異なる場合の扱い (zakuro と同じく `--resolution` に合わせて `ScaleFrom` するか、Y4M の解像度を優先するか)
  - ループ再生の有無 (zakuro の `Y4MReader` はループする)
  - `--fake-capture-device` とファイル指定を同時に指定したときの優先順位
- ファイルを追加する場合は `examples/sumomo/CMakeLists.txt` の `target_sources` を更新する
- `examples/sumomo/README.md` の「映像と音声のデバイスに関するオプション」と `CHANGES.md` の `## develop` に `[ADD]` エントリを追記する

## Pending 理由

- 実装場所 (SDK の `FakeVideoCapturer` を拡張するか、sumomo 固有の capturer と reader を追加するか) が未確定である
- オプション体系 (既存の `--fake-capture-device` の拡張か、ファイル指定用の独立オプションか) が未確定である
- WAV のサンプルレート・チャンネル変換方針と、Y4M の解像度と `--resolution` が異なる場合の扱いが未確定である

## Pending 解除条件

- 実装場所とオプション体系が確定したこと
- WAV のサンプルレート・チャンネル変換方針と、Y4M の解像度の扱いが確定したこと

## 完了条件

- Y4M ファイルと WAV ファイルを指定して sumomo を起動すると、その映像と音声が Sora へ配信されること
- ファイルを指定しない場合、カメラ / マイクおよび `--fake-capture-device` の既存の挙動が変わらないこと
- `examples/sumomo/README.md` にオプションが記載されていること
- `CHANGES.md` の `## develop` に `[ADD]` エントリが追記されていること

## テスト方針

- 手元環境で Y4M / WAV ファイルを指定して Sora へ接続し、映像と音声が配信されることを確認する
- ファイル終端に達したときの挙動 (ループ再生の有無) を確認する
- 既存の `--fake-capture-device` を利用した E2E テストが引き続き通ることを確認する
