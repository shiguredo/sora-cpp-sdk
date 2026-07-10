# webrtc::InitializeSSL() の戻り値が無視されている

- Priority: High
- Created: 2026-07-10
- Completed: {YYYY-MM-DD}
- Model: DeepSeek V4 Pro
- Branch: feature/fix-initialize-ssl-return-value
- Polished: 2026-07-10
## 目的

`src/sora_client_context.cpp` の `Create()` 関数内で `webrtc::InitializeSSL()` の bool 戻り値が無視されている。SSL 初期化に失敗した場合、後続の TLS 接続で不可解なエラーが発生し、原因特定が困難になる。

## 優先度根拠

SSL 初期化失敗は稀だが、発生時のデバッグが極めて困難。戻り値チェックとログ出力は低コストで防御できるため High。

## 現状

`src/sora_client_context.cpp:79`:

```cpp
webrtc::InitializeSSL();
```

戻り値が `bool` であるにもかかわらずチェックされていない。

## 設計方針

戻り値をチェックし、`false` の場合はエラーログを出力して `nullptr` を返す。

## 完了条件

- `InitializeSSL()` の戻り値がチェックされ、失敗時に適切なエラー処理が行われること
- `src/sora_client_context.cpp:79` の呼び出しで戻り値 `false` 時にエラーログ出力と `nullptr` リターンが行われること
- `CHANGES.md` の `## develop` 配下、`### misc` セクションに以下の形式で `[FIX]` エントリを追記する:
  ```
  - [FIX] webrtc::InitializeSSL() の戻り値が無視されているのを修正する
    - @<担当者>
  ```
