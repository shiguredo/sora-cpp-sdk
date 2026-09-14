# HTTP Proxy 対応 Phase 2（OS 設定の自動参照）

- Created: 2026-09-10
- Completed: {YYYY-MM-DD}
- Branch: feature/add-http-proxy-phase2
- Polished: 2026-09-15

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
- 対象プラットフォームは Windows / macOS / Linux とする。iOS / Android では OS のプロキシ設定を参照せず従来どおり（プロキシなし）で接続し、対応が必要になった場合は別 issue で検討する
- 読み取った OS 設定は `proxy_url`（認証情報があれば `proxy_username` / `proxy_password`）へ正規化し、明示設定と同じ適用経路（`SoraSignaling::DoConnect` / `Redirect` / `CreatePeerConnection`）に載せる。プロキシ接続の経路は新たに追加しない
- OS 設定のバイパス指定（Linux の `no_proxy`、Windows のプロキシバイパス、macOS の ExceptionsList）を読み取り、一致するホストへの接続はプロキシなしとする
- 明示設定（`proxy_url` 非空）を優先し、未指定時のみ OS 設定にフォールバックする
- libwebrtc の `ProxyInfo`（`webrtc::revive::ProxyType`）で TURN に適用できるのは `PROXY_HTTPS` のみで、SOCKS5 を使うには専用のソケット実装が必要になる。OS 設定が SOCKS5 を指す場合は本 issue のスコープ外とし、プロキシなしで接続して警告ログを出力する。SOCKS5 対応が必要になった場合は別 issue で検討する
- プロキシの認証情報を OS 設定から取得できるかはプラットフォーム依存のため調査する。取得できる場合は `proxy_username` / `proxy_password` に設定し、取得できない場合は認証なしのプロキシとして扱う
- PAC ファイルの自動解決は本 issue のスコープ外とし、別 issue とする（JavaScript エンジンが必要）
- `network_manager` / `socket_factory` の設定は Phase 1 と同じく利用側の責務とする（SDK 側に `SoraClientContext` から取得するヘルパーは追加しない）。未設定の場合、OS 設定の自動参照で `proxy_url` が決まっても TURN へのプロキシ適用はスキップし、WebSocket (wss) のみプロキシ経由で接続して警告ログを出力する

## 完了条件

- `proxy_url` 未指定でも、OS のプロキシ設定が HTTP プロキシを指す環境（SOCKS5 は対象外）で、WebSocket (wss) と TURN-TCP がプロキシ経由で接続できること。TURN-TCP の確認は `network_manager` / `socket_factory` を設定した利用側（例: sumomo）で行う
- `proxy_url` を指定した場合は従来どおり明示設定が優先され、Phase 1 の挙動が変わらないこと
- プロキシ設定がない環境、または OS 設定が SOCKS5 のみを指す環境では、従来どおりプロキシなしで接続できること
- OS 設定のバイパス指定に一致するホストへはプロキシなしで接続できること
- `CHANGES.md` の `## develop` に `[ADD]` エントリを追記すること
