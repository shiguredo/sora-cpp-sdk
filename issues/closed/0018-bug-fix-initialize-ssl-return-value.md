# webrtc::InitializeSSL() の戻り値が無視されている

- Priority: High
- Created: 2026-07-10
- Completed: 2026-07-13
- Model: DeepSeek V4 Pro
- Branch: feature/fix-initialize-ssl-return-value
- Polished: 2026-07-10
## 目的

`src/sora_client_context.cpp` の `SoraClientContext::Create()` 内で `webrtc::InitializeSSL()` の bool 戻り値が無視されている。SSL 初期化に失敗した場合、後続の TLS 接続で不可解なエラーが発生し、原因特定が困難になる。

## 優先度根拠

SSL 初期化失敗は稀だが、発生時のデバッグが極めて困難。戻り値チェックとログ出力の追加箇所は関数先頭の 1 か所であり、実装量・リスクともに最小限で防御できるため High。

## 現状

`src/sora_client_context.cpp` の `SoraClientContext::Create()` 内:

```cpp
webrtc::InitializeSSL();
```

戻り値が `bool` であるにもかかわらずチェックされていない。

`ssl_adapter.h:119` のコメントには `// Call this on the main thread, before using SSL.` とあり、メインスレッドからの呼び出しが前提とされている。`SoraClientContext::Create()` は呼び出し元スレッドを制約していないが、現在のすべての呼び出し箇所（テストコード・サンプルコード）はメインスレッドから呼ばれており、実運用上の問題は発生していない。本 issue の修正（戻り値チェック追加）によって新たに問題が発生することはない。

また、デストラクタ (`src/sora_client_context.cpp:74`) では `webrtc::CleanupSSL()` が意図的にコメントアウトされている（初期実装時からの設計判断）。`InitializeSSL() / CleanupSSL()` は対になる API だが、本 SDK では SSL リソースの解放をプロセス終了時の OS 任せにしており、`CleanupSSL()` の非呼び出しは既存の設計意図である。本 issue は `InitializeSSL()` の戻り値チェック追加のみを対象とし、`CleanupSSL()` の扱いはスコープ外とする。

## 設計方針

`InitializeSSL()` の戻り値をチェックし、`false` の場合は `RTC_LOG(LS_ERROR) << "Failed to initialize SSL"` を出力して `nullptr` を返す。

- `InitializeSSL()` が `true` を返した場合（成功時）は既存のコードフローをそのまま維持する。
- `InitializeSSL()` は `Create()` の先頭で呼ばれているため、失敗時に `return nullptr` しても後続リソース（`SoraClientContext` の `shared_ptr`、各スレッド）は未生成であり、クリーンアップは不要。
- `Create()` が `nullptr` を返すのは既存のエラーパス（`CreateVideoCodecFactory()` 失敗時、`PeerConnectionFactory` 作成失敗時等）と同様のパターンであり、呼び出し元の null チェックが不足している点は本 issue のスコープ外（既存の問題。別途 issue 化を検討する）。
- `Create()` はメインスレッドからの呼び出しを前提としており、既存の全呼び出し箇所もメインスレッドから呼ばれている。`ssl_adapter.h` に `InitializeSSL()` の「Call this on the main thread」要件が明記されているが、本修正によってこの前提が変わることはない。
- `InitializeSSL()` が `false` を返す状況をモックなしで再現するのは困難なため、本修正では異常系の自動テストは追加しない。正常系の確認は既存のテストをビルド・実行し、リグレッションがないこと（クラッシュしないこと）で検証する。
- Android テストアプリの `JNI_OnLoad`（`test/android/app/src/main/cpp/jni_onload.cc`）では `RTC_CHECK(webrtc::InitializeSSL())` で abort しているが、プロダクションコードでは `Create()` が `nullptr` を返す graceful なエラーハンドリングが適切である。

## 完了条件

- `src/sora_client_context.cpp` の `SoraClientContext::Create()` 先頭で `webrtc::InitializeSSL()` の戻り値 `false` 時に `RTC_LOG(LS_ERROR) << "Failed to initialize SSL"` を出力し `return nullptr` すること
- 既存のテスト（`test/e2e.cpp`、`test/connect_disconnect.cpp`、`test/hello.cpp`、`test/datachannel.cpp`）をビルド・実行し、正常系でクラッシュせず成功すること（リグレッションがないこと）
- `CHANGES.md` の `## develop` 直下（`### misc` より前）の `[FIX]` 群に以下のエントリを追記する:
  ```
  - [FIX] webrtc::InitializeSSL() の戻り値が無視されているのを修正する
    - `SoraClientContext::Create()` で `InitializeSSL()` が `false` を返した場合にエラーログを出力し `nullptr` を返すようにする
    - @<担当者>

## 解決方法

`SoraClientContext::Create()` 先頭の `webrtc::InitializeSSL()` 呼び出しに戻り値チェックを追加した。
`false` が返った場合は `RTC_LOG(LS_ERROR) << "Failed to initialize SSL"` を出力し `nullptr` を返す。

BoringSSL の `OPENSSL_init_ssl` は常に `return 1;` であり現状 `false` を返すことはないが、
将来的な BoringSSL の変更や OpenSSL 利用時に備えて戻り値をチェックする。

  ```
