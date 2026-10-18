# テストとサンプルのログ初期化を InitializeLogging に移行する

- Created: 2026-09-29
- Completed: 2026-10-18
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

## 解決方法

テストとサンプルの計 8 ファイルで、`LogToDebug()` / `LogTimestamps()` / `LogThreads()` の 3 呼び出しを `webrtc::InitializeLogging()` の 1 呼び出しに置き換えた。

### 行った変更

- `webrtc::LoggingConfig` の `set_debug_severity()` に従来 `LogToDebug()` に渡していた重大度、`set_log_timestamp(true)`、`set_log_thread(true)` を設定してから `webrtc::InitializeLogging()` を呼ぶようにした
  - 重大度は `LogToDebug()` が設定していたデバッグ出力の重大度に対応する `debug_severity` を設定する。`min_severity` は `InitializeLogging()` がログ sink の重大度として扱う値なので変更しない
  - `log_thread` を有効にしないとログにスレッド ID が出力されない
- `main` の先頭ではなく、従来ログを設定していた位置で 1 回だけ呼ぶようにした
- `test/hello.cpp` は設定ファイルの `log_level` で重大度を後から設定していたため、初期化を設定ファイルの読み込み後に移し、指定された場合はその値を使うようにした
  - `InitializeLogging()` は 1 プロセスにつき 1 回しか設定を反映しないため、初期化後に `LogToDebug()` を呼んでも重大度は変わらない
- サンプル 3 種は従来どおり `--log-level` が `webrtc::LS_NONE` 以外のときだけ初期化する
  - `LS_NONE` の場合は `InitializeLogging()` を呼ばないため、最初のログ出力時に既定の設定で暗黙に初期化される
- 各ファイルに `std::move` を使うための `#include <utility>` を追加した

### 確認結果

- `test/` の 5 ファイルと、sumomo / sdl_sample をビルドし、`'LogThreads' is deprecated` の警告が出ないことを確認した
- 変更前のビルドでは同じ 5 ファイルで同じ警告が出ることを確認済み
- sumomo を `--log-level verbose` で実行し、ログが `[経過秒:ミリ秒][スレッド ID] (ファイル名:行): メッセージ` の形式で出力されることを確認した
- sumomo を `--log-level none` で実行し、ログが出力されないことを確認した
- messaging_recvonly_sample は CMake のビルドでリンク時に失敗するため、コンパイルのみ確認し、リンクは手動で行って確認した
