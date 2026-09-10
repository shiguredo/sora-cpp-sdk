# HTTP Proxy 対応 Phase 2（OS 設定の自動参照）

- Created: 2026-09-10
- Completed: {YYYY-MM-DD}
- Branch: feature/add-http-proxy-phase2
- Polished: {YYYY-MM-DD}

## 目的

Sora C++ SDK のプロキシ設定が未指定の場合に OS のプロキシ設定を自動参照し、手動設定なしでプロキシ経由の接続を可能にする。企業ネットワーク環境での設定コストを削減する。

## 現状

Phase 1 でプロキシを手動指定できるようになっているが、OS のプロキシ設定は参照していない。

- `include/sora/sora_signaling.h` の `SoraSignalingConfig` が `proxy_url` / `proxy_username` / `proxy_password` / `proxy_agent` を持つ。`network_manager` と `socket_factory` はプロキシ利用時に必須
- `src/sora_signaling.cpp` の `SoraSignaling::DoConnect` と `SoraSignaling::Redirect` は `proxy_url` が空でなければ `Websocket::https_proxy_tag` のコンストラクタを使い、WebSocket (wss) を HTTPS プロキシの CONNECT 経由にする
- `src/sora_signaling.cpp` の `SoraSignaling::CreatePeerConnection` は `proxy_url` が空でなければ `webrtc::BasicPortAllocator::set_proxy` を呼び、TURN-TCP をプロキシ経由にする
- `examples/sumomo/src/sumomo.cpp` は `--proxy-url` / `--proxy-username` / `--proxy-password` で手動設定し、`SoraClientContext` から `default_network_manager()` / `default_socket_factory()` を取得して `SoraSignalingConfig` に設定する
- `proxy_url` が空の場合はプロキシなしで接続する
- 環境変数や OS のプロキシ設定を読むコードは存在しない

## 設計方針

- `proxy_url` が空の場合に OS のプロキシ設定を読む経路を追加する。読み取り方法はプラットフォームで異なるため、調査したうえで実装する
  - Windows: `WinHttpGetIEProxyConfigForCurrentUser` / `WinHttpGetProxyForUrl`
  - macOS: `CFNetworkCopySystemProxySettings` または `SCDynamicStoreCopyProxies`
  - Linux: 環境変数 `http_proxy` / `https_proxy` / `all_proxy` / `no_proxy`
- 明示設定（`proxy_url` 非空）を優先し、未指定時のみ OS 設定にフォールバックする
- libwebrtc の `ProxyInfo` で TURN に適用できるのは HTTPS プロキシのみで、SOCKS5 を使うには専用のソケット実装が必要になる。OS 設定が SOCKS5 を指す場合の扱いを調査する
- プロキシの認証情報を OS 設定から取得できるかはプラットフォーム依存のため調査する
- PAC ファイルの自動解決は本 issue のスコープ外とし、別 issue とする（JavaScript エンジンが必要）
- `network_manager` / `socket_factory` を必須にしたままでは自動参照できないため、`SoraClientContext` から取得するヘルパーを SDK が提供するか、利用側の責務のままとするかを決める

## 完了条件

- `proxy_url` 未指定でも、OS のプロキシ設定がある環境で WebSocket (wss) と TURN-TCP がプロキシ経由で接続できること
- `proxy_url` を指定した場合は従来どおり明示設定が優先され、Phase 1 の挙動が変わらないこと
- プロキシ設定がない環境では従来どおりプロキシなしで接続できること
- `CHANGES.md` の `## develop` に `[ADD]` エントリを追記すること
