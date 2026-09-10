# ログに UTC タイムスタンプを追加する

- Created: 2026-09-10
- Completed: {YYYY-MM-DD}
- Branch: feature/add-log-timestamp
- Polished: {YYYY-MM-DD}

## 目的

Sora C++ SDK が出力するログに UTC のタイムスタンプを追加する。Android SDK / iOS SDK に C++ SDK を組み込んだ際に、ログの発生時刻を特定できるようにするため。

## 現状

- SDK 本体・サンプル・テストは libwebrtc の `webrtc::LogMessage` 系 API（`RTC_LOG`）でログを出力している
- `examples/sumomo/src/sumomo.cpp` は `webrtc::LogMessage::LogToDebug` / `LogTimestamps` / `LogThreads` を呼んでおり、`test/` の各テストも同様
- `webrtc::LogMessage::LogTimestamps` は libwebrtc のヘッダコメントに「Display the elapsed time of the program」とあるとおり、プログラム開始からの経過時間を `[秒:ミリ秒]` 形式で出力する機能で、壁時計の時刻（UTC）は出力しない
- libwebrtc 内部では `LogLineRef::timestamp()` もログ開始からの経過時間が設定されており、絶対時刻は保持していない。絶対時刻が必要な場合は `webrtc::LogMessage::WallClockStartTime()` と経過時間を組み合わせるか、sink 側で現在時刻を取得する必要がある
- `webrtc::LogSink` を実装して `webrtc::LogMessage::AddLogToStream` で登録するとログの書式をカスタムできる。`OnLogMessage(const LogLineRef&)` を override すれば、ファイル名・行・スレッド・メッセージに加えて任意のタイムスタンプを整形して出力できる
- 現状 SDK はログの出力先・書式を管理しておらず、アプリ・サンプル・テストが `webrtc::LogMessage` を直接設定している
- 既定の stderr 出力（`LogToDebug`）とカスタム sink を併用すると二重に出力されるため、どちらを使うかを整理する必要がある

## 設計方針

- SDK がログ出力を初期化する API を提供し、UTC タイムスタンプ付きのカスタム `webrtc::LogSink` を登録する
- カスタム `LogSink` は `OnLogMessage(const LogLineRef&)` を override し、`YYYY-MM-DDThh:mm:ss.sssZ` のような UTC 形式のタイムスタンプを先頭に付けて出力する。時刻は sink 側で取得するか、`WallClockStartTime()` と `LogLineRef::timestamp()` から算出する
- 既存の重大度・スレッド名・ファイル名・行番号・メッセージの出力は維持する
- 既定の stderr 出力を使うかカスタム sink に一本化するかを整理し、二重出力を避ける
- タイムスタンプの書式・ログレベルを API で指定できるようにするか SDK 固定とするかを決める
- ログ初期化 API を `SoraClientContext` に含めるか、専用の関数として提供するかを決める

## 完了条件

- SDK の提供する方法でログを初期化すると、すべてのログに UTC タイムスタンプが付与されること
- 既存の重大度・スレッド名・ファイル名・行番号の出力が維持されること
- sumomo で UTC タイムスタンプ付きのログが出力されることを確認していること
- `CHANGES.md` の `## develop` に `[ADD]` エントリを追記していること

## Pending 理由

- sora-oss-private の issue で「不急」ラベルが付いており優先度が低い
- libwebrtc には経過時間を出力する `LogTimestamps` はあるが UTC タイムスタンプを出力する仕組みがなく、カスタム `LogSink` の実装と既定の出力先の切り替えが必要で、tnoho さんからも「かなりめんどくさい」と共有されている
- SDK がログ出力をどこまで管理するか（`SoraClientContext` に含めるか、専用 API にするか、アプリに委ねるか）の設計判断が必要

## Pending 解除条件

- ログ初期化 API の置き場所とタイムスタンプの書式が確定したこと
- カスタム `LogSink` による UTC 出力と既定出力の扱いが確定したこと
