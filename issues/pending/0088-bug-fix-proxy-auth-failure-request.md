# HTTP Proxy 認証に失敗したあとに不要なリクエストを送る

- Created: 2026-09-11
- Completed: {YYYY-MM-DD}
- Branch: feature/fix-proxy-auth-failure-request
- Polished: {YYYY-MM-DD}

## 目的

HTTP Proxy の認証に失敗した場合に、SDK が不要なリクエスト (TLS Client Hello) を送ってしまう問題を修正する。

## 現状

- HTTP Proxy 経由で wss に接続する際、プロキシが `407 Proxy Authentication Required` を返しても、SDK が TLS の Client Hello を同じコネクションに送ってしまう
- Squid の access.log では `TCP_DENIED/407` の直後に `NONE_NONE/400 error:invalid-request` が出る
- 原因は `src/websocket.cpp` の `Websocket::OnReadProxy` が CONNECT レスポンスのステータスコードを確認せず、常に `wss_` を作成して TLS ハンドシェイクを開始しているため。`proxy_resp_.result()` を見ていない
- 2024.6.1 でも再現し、デグレではない
- 再現環境: macOS (Sonoma 14.4.1)、Squid 5.7、HTTP Proxy 認証あり、sumomo にプロキシの認証情報を渡さない
- 再現手順: `--proxy-url` のみ指定して sumomo を実行し、Squid の access.log と TShark で確認する

## 設計方針

- `Websocket::OnReadProxy` で `proxy_resp_.result()` を確認し、CONNECT が成功 (2xx、通常は 200) していない場合は `on_connect` にエラーを渡して TLS ハンドシェイクを開始しない
- プロキシ認証失敗などのエラーを利用者に伝える方法を決める。`boost::asio::error::access_denied` などの既存のエラーコードを使うか、独自のエラーを設けるかを検討する
- 認証情報を指定している場合と指定していない場合の挙動を整理する。再試行や `407` の `Proxy-Authenticate` の扱いを検討する
- 認証付き HTTP Proxy (Squid) 環境で再現・確認する

## 完了条件

- プロキシが `407` を返した場合に TLS ハンドシェイクを開始せず、接続エラーとして扱われること
- `407` のあとに `400 error:invalid-request` が発生しないこと
- プロキシ認証が成功する場合は従来どおり接続できること

## Pending 理由

- 再現と確認に認証付きの HTTP Proxy (Squid) 環境とパケットキャプチャが必要で、通常の CI / E2E では確認できない
- プロキシ認証失敗時のエラーの扱い (エラーコード、再試行、`Proxy-Authenticate` の処理) に設計判断が必要

## Pending 解除条件

- プロキシ認証失敗時の挙動 (エラーコードと再試行の有無) の方針が決まったこと
- 認証付き HTTP Proxy 環境で再現と確認ができる見込みが立ったこと
