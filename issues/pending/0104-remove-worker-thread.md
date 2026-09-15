# SoraClientContext から worker_thread を削除する

- Created: 2026-09-15
- Completed: {YYYY-MM-DD}
- Branch: feature/remove-worker-thread
- Polished: {YYYY-MM-DD}

## 目的

libwebrtc の issue 558821261「Deprecate and remove PeerConnectionFactoryDependencies::worker_thread」の削除系 CL (499302 / 501640 / 501720 / 502000 / 502500 / 502860 / 502940 / 502960) が取り込まれると、`PeerConnectionFactoryDependencies::worker_thread` と `PeerConnectionFactoryInterface::worker_thread()` が削除される。Sora C++ SDK に残っている worker_thread 関連の参照を無くし、公開ヘッダから `SoraClientContext::worker_thread()` を削除する。

なお、専用 worker thread の生成をやめて network thread を使うようにする対応 (方針1) は別 issue で先に行う。本 issue は方針1 の後も残る worker_thread の参照と API を削除する対応 (方針2) であり、時間軸が異なるため issue を分けている。

## 現状

- 方針1 の後も、次の worker_thread 関連の記述が残る
  - `src/sora_client_context.cpp` の `dependencies.worker_thread` に network thread を渡している行
  - `include/sora/sora_client_context.h` の `SoraClientContext::worker_thread()` アクセサ
  - `include/sora/sora_client_context.h` の `worker_thread_` メンバ
  - `src/sora_peer_connection_factory.cpp` の `webrtc::PeerConnectionFactoryProxy::Create` に渡している `factory->worker_thread()`
- 依存は `DEPS` の `WEBRTC_BUILD_VERSION=m154.8037.1.1`
- `SoraClientContext::worker_thread()` は公開ヘッダにあるため、削除は利用側に対する後方互換のない変更になる

## 前提条件

- webrtc-build が 558821261 の削除系 CL を含むバージョンをリリースしていること。現時点ではそのバージョンは存在しない
- 公開ヘッダから `worker_thread()` が消える後方互換のない変更に、sora-unity-sdk / sora-python-sdk / zakuro が追随済みであること
- 上記が満たされるまで着手できない

## 設計方針

- `include/sora/sora_client_context.h` から `worker_thread()` アクセサと `worker_thread_` メンバを削除する
- `src/sora_client_context.cpp` から `dependencies.worker_thread` に network thread を渡している行を削除する
- `src/sora_peer_connection_factory.cpp` の `factory->worker_thread()` を `factory->network_thread()` に置き換える
- 後方互換のない変更のため、`CHANGES.md` の `## develop` に `[CHANGE]` を記載する

## 完了条件

- Sora C++ SDK に worker_thread 関連の参照が 0 件になっていること
- ビルドとテストが通ること
- `CHANGES.md` の `## develop` に `[CHANGE]` が記載されていること
- sora-unity-sdk / sora-python-sdk / zakuro の追随が完了していること

## Pending 理由

- 前提条件の「webrtc-build が 558821261 の削除系 CL (499302 / 501640 / 501720 / 502000 / 502500 / 502860 / 502940 / 502960) を含むバージョンをリリースしていること」が未成立である
  - 0103 (issue `refactor-unify-worker-thread.md`) の記載では削除系 CL はレビュー中のままである
  - 現行 `DEPS` の `WEBRTC_BUILD_VERSION=m154.8037.1.1` に削除系 CL は含まれていない
- 方針1 にあたる 0103 の実装が未完了である (`src/sora_client_context.cpp` の `SoraClientContext::Create` に専用 worker thread の生成が残っている)
- sora-unity-sdk / sora-python-sdk / zakuro の追随完了が未確認である
- 上記はいずれも本リポジトリの作業だけでは解消できず、現時点では着手できないため保留とする

## Pending 解除条件

- webrtc-build が削除系 CL を含むバージョンをリリースし、`DEPS` を更新する見込みが立ったこと
- 0103 の実装が完了していること
- sora-unity-sdk / sora-python-sdk / zakuro が公開ヘッダからの `worker_thread()` 削除に追随済みであること
