# OnTrack の仕様を整理してドキュメントに記載する

- Created: 2026-09-10
- Completed: {YYYY-MM-DD}
- Branch: feature/update-ontrack-documentation
- Polished: {YYYY-MM-DD}

## 目的

Sora C++ SDK の `OnTrack` / `OnRemoveTrack` の仕様を整理し、上位 SDK（Sora Flutter SDK / Sora Unity SDK）が提供する `onAddTrack` との違いをドキュメントに記載する。

`onAddTrack` を libwebrtc / C++ SDK の `OnTrack` と同一視すると、ローカル映像の表示や再描画のタイミングを正しく実装できない。C++ SDK を直接利用するアプリの実装者と、C++ SDK を組み込む上位 SDK の実装者が、通知範囲と表示トラック管理の責務を理解できるようにするため。

## 現状

調査で判明した内容は以下のとおり。

- `include/sora/sora_signaling.h` の `SoraSignalingObserver::OnTrack` は `webrtc::RtpTransceiverInterface` を、`OnRemoveTrack` は `webrtc::RtpReceiverInterface` を受け取る
- `src/sora_signaling.cpp` の `SoraSignaling::OnTrack` / `SoraSignaling::OnRemoveTrack` は libwebrtc の `webrtc::PeerConnectionObserver::OnTrack` / `OnRemoveTrack` に対応し、io_context にポストしてから observer を呼ぶ
- libwebrtc の `OnTrack` は「シグナリングにより、トランシーバーがリモートエンドポイントからメディアを受信することになったとき」に呼ばれる。`SetRemoteDescription` の中で発生し、Unified Plan セマンティクスの場合にのみ呼ばれる。受信トラックは `transceiver->receiver()->track()`、関連するストリームは `transceiver->receiver()->streams()` から取得できる
- 自身が送信するトラック（カメラ映像・マイク音声など）はアプリ自身が `AddTrack` で追加するため、`OnTrack` では通知されない
- Sora Flutter SDK / Sora Unity SDK は `onAddTrack` という独自のコールバックを提供している。これは C++ SDK の `OnTrack` とは別の概念で、次の 2 つをまとめて「画面表示に利用するビデオトラックが増えた」として通知する
  - C++ SDK の `OnTrack` で通知されたメディアが映像だったとき
  - 自身のビデオトラック（カメラで取得した映像）を作成したとき
- 上位 SDK は受信映像をテクスチャなどへ書き込む処理を実装しており、`onAddTrack` は再描画のトリガーとして利用している。たとえば Flutter SDK の example は `onAddTrack` で `setState` を呼んでおり、この通知がないとトラックの増減に気付けず、他の参加者の入室・退室が画面に反映されない
- C++ SDK 自身は描画機能を持たず、表示に利用するトラックの管理はアプリの責務になっている。`examples/sumomo/src/sumomo.cpp` では、ローカルのカメラ映像はトラック作成時に、リモートの映像は `Sumomo::OnTrack` / `Sumomo::OnRemoveTrack` でレンダラーに追加・削除している
- `doc/` にはビルド・開発・FAQ・既知の問題のドキュメントがあるが、`OnTrack` / `OnRemoveTrack` の仕様や表示トラック管理の責務についての記載はない

## 設計方針

- `OnTrack` / `OnRemoveTrack` の呼び出し条件を整理してドキュメントに記載する
  - リモートの音声・映像トラックが追加・削除されたときに呼ばれる。ローカルトラックは対象外である
  - 受信トラックは `transceiver->receiver()->track()`、関連ストリームは `transceiver->receiver()->streams()` から取得できる
- 「表示に利用するビデオトラックの増減」を通知する専用のコールバックは C++ SDK には存在せず、上位 SDK やアプリが `OnTrack` とローカルトラックの作成を組み合わせて実装するものであることを記載する
- 上位 SDK 固有の `onAddTrack` の詳細は各 SDK のドキュメントに委ね、C++ SDK のドキュメントには通知範囲と責務の境界を記載する
- 記載先は `doc/` 配下のドキュメントとし、具体的なファイルと記載量は実装時に決める

## 完了条件

- `OnTrack` / `OnRemoveTrack` の仕様（呼び出し条件、引数、ローカルトラックが対象外であること）がドキュメントに記載されていること
- 表示に利用するトラックの管理がアプリ・上位 SDK の責務であることがドキュメントに記載されていること
- 上位 SDK の `onAddTrack` が C++ SDK の `OnTrack` を利用しつつ別の概念であることがドキュメントに記載されていること

## Pending 理由

- ドキュメントの記載先と記載量（`doc/` のどのファイルにどこまで書くか）が未確定である
- 上位 SDK 固有の `onAddTrack` を C++ SDK 側のドキュメントでどこまで扱うか、上位 SDK 側のドキュメントに委ねる範囲の切り分けが必要である

## Pending 解除条件

- ドキュメントの記載先と記載内容が確定したこと
