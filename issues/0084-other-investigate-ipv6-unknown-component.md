# IPv6 が有効な時に Candidate has an unknown component が出力されるのを調査する

- Created: 2026-09-11
- Completed: {YYYY-MM-DD}
- Branch: feature/fix-ipv6-unknown-component
- Polished: 2026-09-15

## 目的

Sora の設定で `ipv6 = true` にしたときに、C++ SDK (sumomo) で `Candidate has an unknown component` が出力される事象を調査し、C++ SDK 側で対処できる場合は修正する。この警告は libwebrtc が `RTC_LOG(LS_WARNING)` で出力するものであり (現行 DEPS の M154.8037.1.1 で確認)、ログが汚れて本物の問題との切り分けがしづらくなるため。

## 現状

- Sora の設定で `ipv6 = true` にした場合に、libwebrtc の `PeerConnection::AddRemoteCandidate` (`pc/peer_connection.cc`) が `Candidate has an unknown component` を `RTC_LOG(LS_WARNING)` で出力する。この文言は `JsepTransport::AddRemoteCandidates` (`pc/jsep_transport.cc`) が返すエラー (`RTCError`) のメッセージであり、現行 DEPS (M154.8037.1.1) で確認した
  - `mid data`: `[002:615][4355] (peer_connection.cc:2675): Candidate has an unknown component: Cand[:1:3:udp:3:[2001:db8:0:x:x:x:x:x]:11490:host::0:...] for mid data`
  - `mid audio_*`: `[001:280][4867] (peer_connection.cc:2675): Candidate has an unknown component: Cand[:1:3:udp:3:[2001:db8:0:x:x:x:x:x]:11490:host::0:...] for mid audio_5pojxf`
- 観測された `Cand[:1:3:udp:3:[2001:db8:0:x:x:x:x:x]:11490:host::0:...]` は component 3 (priority も 3) の IPv6 host 候補である。Sora サーバ (`shiguredo/sora`) の `sora_sdp.erl` の `candidates/3` は `lists:mapfoldl` の累積値でアドレスごとに `component_id` を 1, 3, 5, ... と採番するため、`ipv6 = true` で IPv4 + IPv6 の候補が払い出された場合に IPv6 側が component 3 になる
- `ipv6` を未指定にした場合は出力されない
- Sora の設定を減らした場合 (`ipv6_only = true`、`external_signaling_url` のみなど) でも出力される
- 送受信自体は問題なく機能している
- C++ SDK はリモート候補を `webrtc::PeerConnectionInterface::AddIceCandidate` で明示的に追加していない (`grep -rn "AddIceCandidate" src include` は 0 件)。Sora から受信した offer の SDP を `src/session_description.cpp` の `SessionDescription::SetOffer` でそのまま `SetRemoteDescription` している
- `src/sora_signaling.cpp` の websocket メッセージ処理 (`offer` / `redirect` / `update` / `re-offer` / `notify` / `push` / `ping` / `switched`) に `type: candidate` を受信して追加する分岐はない
- C++ SDK 側の `OnIceCandidate` は送信側の候補を Sora へ送る処理のみ
- `CHANGES.md` や issues に対応の記録はない

## 設計方針

- `Candidate has an unknown component` がどの経路で出力されるかを特定する。C++ SDK が `AddIceCandidate` を呼んでいないため、libwebrtc が offer の SDP に含まれる候補を処理する経路か、IPv6 候補の mid / component の解釈に起因するかを切り分ける
- IPv6 有効時に Sora が払い出す候補の内容 (mid と component) と、警告の `Cand[...]` の component の値を確認する
- C++ SDK 側で解消できるかを検討する (例: 受信した offer の SDP を `SetRemoteDescription` に渡す前に加工して `component_id` が不正な候補を除去する)
- 解消が libwebrtc 側の修正になる場合、または原因が Sora サーバ側の SDP 生成 (候補の `component_id` の採番) にある場合は、その旨を issue に記録し、SDK 側では対応しない判断を含めて対応先を検討する
- 対処後も送受信が機能し続けることを確認する

## 完了条件

- `Candidate has an unknown component` が出力される経路が特定されていること
- C++ SDK 側で対処できる場合、`ipv6 = true` の Sora へ接続して警告が出力されなくなること
- `ipv6 = true` の Sora へ接続して送受信が引き続き機能すること
- 原因が C++ SDK 側の対処では解消できない場合 (libwebrtc 側、または Sora サーバ側の SDP 生成に起因する場合)、その調査結果と対応先の判断が issue に記録されていること
