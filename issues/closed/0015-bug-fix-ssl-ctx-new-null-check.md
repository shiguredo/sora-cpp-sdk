# SSL_CTX_new の戻り値未チェックで null deref の可能性がある

- Priority: High
- Created: 2026-07-10
- Completed: 2026-07-13
- Model: DeepSeek V4 Pro
- Branch: feature/fix-ssl-ctx-new-null-check
- Polished: 2026-07-10

## 目的

`src/websocket.cpp` の `CreateSSLContext()` 内で `::SSL_CTX_new(::TLS_method())` の戻り値 `handle` が nullptr チェックされないまま、後続の `SSL_CTX_set_min_proto_version()` / `SSL_CTX_set_max_proto_version()` / `std::make_shared<boost::asio::ssl::context>(handle)` に渡されている。`SSL_CTX_new()` はメモリ不足時に nullptr を返すため、その場合に null deref でクラッシュする。`SSL_CTX_new()` 直後に nullptr チェックを追加し、null 時はエラーログを出力して例外を投げるようにする。

## 優先度根拠

`SSL_CTX_new()` の失敗はメモリ不足時に発生する稀なケースだが、`CreateSSLContext()` は SSL/TLS を使う WebSocket 接続 (`wss` / HTTPS proxy) の初期化パスで必ず通るため、発生すると回避不能なクラッシュとなる。修正はチェック 1 か所の追加でリスク・実装量ともに最小限のため High。

## 現状

`src/websocket.cpp:64-67`:

```cpp
SSL_CTX* handle = ::SSL_CTX_new(::TLS_method());
SSL_CTX_set_min_proto_version(handle, TLS1_2_VERSION);  // handle が nullptr だと null deref
SSL_CTX_set_max_proto_version(handle, TLS1_3_VERSION);  // 同上
auto ctx = std::make_shared<boost::asio::ssl::context>(handle);  // 下記のとおり handle が nullptr だと例外
```

`::SSL_CTX_new()` は OpenSSL / BoringSSL の仕様上、メモリ不足時に nullptr を返す。nullptr が返ると 65 行目・66 行目の `SSL_CTX_set_*_proto_version(nullptr, ...)` で null deref が発生する。

67 行目の `std::make_shared<boost::asio::ssl::context>(handle)` が呼ぶ `boost::asio::ssl::context` の native_handle コンストラクタは、null handle に対して `boost::asio::error::invalid_argument` 例外を投げる (`boost/asio/ssl/impl/context.ipp:375-383`):

```cpp
context::context(context::native_handle_type native_handle)
  : handle_(native_handle)
{
  if (!handle_)
  {
    boost::asio::detail::throw_error(
        boost::asio::error::invalid_argument, "context");
  }
}
```

つまり `Websocket` の SSL コンストラクタは元来「SSL コンテキスト生成失敗時に例外を投げうる」契約になっている。問題は、65-66 行目の OpenSSL API 呼び出し (null deref / クラッシュ) が 67 行目の boost の例外送出より先に発生してしまう点にある。

`CreateSSLContext()` は 2 つのコンストラクタから呼ばれ、戻り値 `ssl_ctx_` の使われ方が異なる:

- `Websocket(ssl_tag, ...)` (`src/websocket.cpp:106-121`): 118 行目で `ssl_ctx_ = CreateSSLContext(...)` の直後、119 行目で `wss_.reset(new ssl_websocket_t(ioc, *ssl_ctx_))` と即座に `*ssl_ctx_` を dereference する。
- `Websocket(https_proxy_tag, ...)` (`src/websocket.cpp:129-150`): 149 行目で `ssl_ctx_ = CreateSSLContext(...)` を格納するのみで、`*ssl_ctx_` の dereference は後続の `OnReadProxy()` (`src/websocket.cpp:476`) まで遅延される。

このため「`CreateSSLContext()` が空の `shared_ptr` を返し、呼び出し元でエラーハンドリングする」という方針は成立しない。呼び出し元のコンストラクタは戻り値を即 dereference するうえ、コンストラクタは戻り値でエラーを返す手段を持たないためである。

## 設計方針

`CreateSSLContext()` 内の `::SSL_CTX_new()` 呼び出し (64 行目) の直後に `handle` の nullptr チェックを追加する。null の場合は `ERR_get_error()` で OpenSSL のエラー情報を取得してログ出力し、例外を投げる。

- 「空の `shared_ptr` を返す」案は採用しない。上記「現状」のとおり、呼び出し元のコンストラクタ (118-119 行目、149 行目) が戻り値を即 `*ssl_ctx_` で dereference し、かつコンストラクタは戻り値でエラーを返せないためである。
- 例外の型は boost が同じ null handle に対して投げる型 (`boost::system::system_error`) に揃える。ただし error_code の中身は `boost::asio::error::invalid_argument` ではなく、`ERR_get_error()` の値と `boost::asio::error::get_ssl_category()` から構築した OpenSSL 固有のエラーコードとする。これにより boost の投げる汎用エラーよりも診断情報が増える。`ERR_get_error()` と `boost::asio::error::get_ssl_category()` の組み合わせは既存コード (`src/websocket.cpp:225`、`src/websocket.cpp:482`) でも SSL エラーの表現に使われており、これに倣う。
- 例外送出に必要な `#include <boost/system/system_error.hpp>` を `src/websocket.cpp` に追加する。現在 `src/websocket.cpp` は `<boost/system/detail/error_code.hpp>` 等はインクルードしているが `system_error` 型のヘッダはインクルードしていないため、追加が必要。
- 正常系のコードパスは変更しない。本修正は現状 65-66 行目で発生している null deref (プロセスクラッシュ) を、より早い段階での明示的な例外送出に置き換えるものである。現状の null deref では 67 行目の boost の例外送出パスには到達しないため、修正後に例外が投げられるのは新設のパスである。例外が捕捉されなければ修正後もプロセスは停止するが、その前にエラーログが残る分だけ診断性が向上する。
- 呼び出し元 (`src/sora_signaling.cpp` の `new Websocket(...)`、271 / 275 / 1272 / 1276 行目) は現状この例外を try-catch していない。現状は同じ状況で null deref によりプロセスがクラッシュしているため、修正によって挙動が悪化することはない。呼び出し元での例外捕捉・graceful な切断処理の追加は本 issue のスコープ外とする (既存の問題であり、必要なら別途 issue 化を検討する)。
- `SSL_CTX_set_min_proto_version()` / `SSL_CTX_set_max_proto_version()` は handle が有効なら失敗しないため、これらの戻り値チェックは対象外とする。`::TLS_method()` の戻り値 null (OpenSSL 初期化失敗) は実運用上発生せず、本 issue のスコープ外とする。

変更後 (64 行目直後):

```cpp
SSL_CTX* handle = ::SSL_CTX_new(::TLS_method());
if (handle == nullptr) {
  boost::system::error_code ec{static_cast<int>(::ERR_get_error()),
                               boost::asio::error::get_ssl_category()};
  RTC_LOG(LS_ERROR) << "Failed to SSL_CTX_new: " << ec.message();
  throw boost::system::system_error(ec);
}
SSL_CTX_set_min_proto_version(handle, TLS1_2_VERSION);
SSL_CTX_set_max_proto_version(handle, TLS1_3_VERSION);
auto ctx = std::make_shared<boost::asio::ssl::context>(handle);
```

## 完了条件

- `::SSL_CTX_new()` が nullptr を返した場合に、65-66 行目の `SSL_CTX_set_*_proto_version()` へ null を渡す前に処理を打ち切り、null deref が発生しないこと
- null 時に `ERR_get_error()` を用いた OpenSSL エラー情報を `RTC_LOG(LS_ERROR)` で出力し、`boost::system::system_error` を送出すること
- `src/websocket.cpp` に `#include <boost/system/system_error.hpp>` が追加されていること
- `::SSL_CTX_new()` が有効な `handle` を返す正常系では、既存のコードフロー・挙動が一切変わらないこと
- `::SSL_CTX_new()` が nullptr を返す状況をモックなしで再現するのは困難なため、異常系の自動テストは追加しない (AGENTS.md「モックやスタブは絶対に利用しないこと」に従う)。正常系は既存のテスト (`test/e2e.cpp`、`test/connect_disconnect.cpp` 等) をビルド・実行し、`wss` 接続でリグレッション (クラッシュ) が発生しないことで検証する
- `CHANGES.md` の `## develop` 直下（`### misc` セクションより前）に `[FIX]` エントリを追記する。`### misc` は Examples / CI / tooling 用のため使わない:
  ```
  - [FIX] `SSL_CTX_new` の戻り値未チェックで null deref の可能性があるのを修正する
    - `CreateSSLContext()` で `SSL_CTX_new()` が nullptr を返した場合にエラーログを出力し例外を送出するようにする
    - @<担当者>

## 解決方法

`src/websocket.cpp` の `CreateSSLContext()` 内で `::SSL_CTX_new(::TLS_method())` の直後に nullptr チェックを追加した。nullptr の場合は `ERR_get_error()` でエラー情報を取得し、
`RTC_LOG(LS_ERROR)` でログ出力したうえで `boost::system::system_error` 例外を送出する。
また、例外送出に必要な `#include <boost/system/system_error.hpp>` を追加した。
  ```
