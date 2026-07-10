# SSL_CTX_new の戻り値未チェックで null deref の可能性がある

- Priority: High
- Created: 2026-07-10
- Polished: 2026-07-10

## 目的

`src/websocket.cpp` の `CreateSSLContext()` 関数内で `SSL_CTX_new()` の戻り値 `handle` が null チェックされずに後続の `SSL_CTX_set_min_proto_version()` 等に渡されている。メモリ不足時に null deref でクラッシュする可能性がある。

## 優先度根拠

本番環境でメモリ不足が発生した場合にプロセスがクラッシュする。WebSocket 接続確立は SDK の初期化パスで必ず通るため、回避不能な致命バグ。High。

## 現状

`src/websocket.cpp:64-66`:

```cpp
SSL_CTX* handle = ::SSL_CTX_new(::TLS_method());
SSL_CTX_set_min_proto_version(handle, TLS1_2_VERSION);  // handle が nullptr だと UB
SSL_CTX_set_max_proto_version(handle, TLS1_3_VERSION);  // 同上
auto ctx = std::make_shared<boost::asio::ssl::context>(handle); // 同上
```

`SSL_CTX_new()` は OpenSSL の仕様上、メモリ不足時に `nullptr` を返す。

## 設計方針

`handle` の null チェックを追加し、失敗時は `nullptr` を返すか、例外を投げる。戻り値が `std::shared_ptr` なので、null チェック失敗時は空の `shared_ptr` を返し、呼び出し元でエラーハンドリングする。

## 完了条件

- `SSL_CTX_new()` の戻り値が null の場合に null deref せずにエラー処理されること
- `src/websocket.cpp:64-66` の `CreateSSLContext()` 内で `handle == nullptr` 時に空の `shared_ptr` を返し、呼び出し元で適切に処理されること
- `CHANGES.md` の `## develop` 配下、`### misc` セクションに以下の形式で `[FIX]` エントリを追記する:
  ```
  - [FIX] SSL_CTX_new の戻り値未チェックで null deref の可能性があるのを修正する
    - @<担当者>
  ```
