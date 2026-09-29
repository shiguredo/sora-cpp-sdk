# テストとサンプルのログ初期化を InitializeLogging に移行する

- Created: 2026-09-29
- Completed: {YYYY-MM-DD}
- Branch: feature/refactor-migrate-to-initializelogging
- Polished: {YYYY-MM-DD}

## 目的

libwebrtc の `webrtc::LogMessage::LogThreads()` は `[[deprecated("Use InitializeLogging instead.")]]` になっており、
テストとサンプルのビルドで deprecation 警告が出ている。将来の libwebrtc で削除されるとビルドできなくなるため、
推奨 API の `webrtc::InitializeLogging()` に移行する。

## 現状

- `test/` の `main` が `webrtc::LogMessage::LogThreads()` を呼んでいる (5 ファイル)
  - `test/hello.cpp` / `test/e2e.cpp` / `test/datachannel.cpp` / `test/device_list.cpp` / `test/audio_adaptive_ptime.cpp`
- `examples/` の `main` が `webrtc::LogMessage::LogThreads()` を呼んでいる (3 ファイル)
  - `examples/sumomo/src/sumomo.cpp` / `examples/sdl_sample/src/sdl_sample.cpp` / `examples/messaging_recvonly_sample/src/messaging_recvonly_sample.cpp`
- いずれも `LogToDebug(severity)` → `LogTimestamps()` → `LogThreads()` の順で呼んでいる
- サンプルは `--log-level` が `webrtc::LS_NONE` 以外のときだけログを設定している
- `test/ios/hello/ViewController.mm` と `test/android/app/src/main/cpp/jni_onload.cc` の `LogThreads()` はコメントアウト済みで対象外
- deprecated なのは `LogThreads()` と `ConfigureLogging()` の 2 つで、`ConfigureLogging()` は SDK 内で未使用 (grep で確認済み)
- deprecation は m155 で新たに付いたものではなく、m150.7871.3.0 と m154.8037.1.2 の prebuilt のヘッダにも存在する
- `webrtc::InitializeLogging(const LoggingConfig&)` は `LoggingConfig` で最小重大度・デバッグ重大度・スレッド名・タイムスタンプ・stderr 出力などを一括設定でき、1 プロセスにつき 1 回だけ呼べる。m155.8059.1.1 の macOS arm64 prebuilt の `libwebrtc.a` に `webrtc::InitializeLogging(webrtc::LoggingConfig)` のシンボルがあることを確認済み
- ビルドは警告のみで成功しており、CI も失敗していない
- 関連 issue: `issues/pending/0066-add-log-timestamp.md` (SDK の UTC タイムスタンプ付きログ)。本 issue は deprecated API の移行であり目的が異なる

## 設計方針

- `LogToDebug()` / `LogTimestamps()` / `LogThreads()` の 3 呼び出しを `webrtc::InitializeLogging()` の 1 呼び出しに置き換える
  - `LoggingConfig` の重大度・`set_log_timestamp(true)`・`set_log_thread(true)` を設定し、ログの出力内容を現状と同じにする
  - サンプルは `LS_NONE` の場合の扱いを現状に合わせる
- `main` の先頭で 1 回だけ呼ぶ
- SDK 本体 (`src/` / `include/`) は `LogThreads()` を使っていないため変更しない

## 完了条件

- `test/` と `examples/` から `webrtc::LogMessage::LogThreads()` の呼び出しが無くなっていること
- テストとサンプルのビルドで `LogThreads` の deprecation 警告が出ないこと
- テストとサンプルのログにタイムスタンプとスレッド名が従来どおり出力されること
- `CHANGES.md` の `## develop` の `### misc` にエントリを追記していること
