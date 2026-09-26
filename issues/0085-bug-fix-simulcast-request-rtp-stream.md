# Intel VPL 環境でサイマルキャスト送信時に RequestSimulcastRid API で解像度が切り替わらない

- Created: 2026-09-11
- Completed: {YYYY-MM-DD}
- Branch: feature/fix-simulcast-request-rid
- Polished: 2026-09-14

## 目的

AV1 サイマルキャストを送信している接続で、Sora の HTTP API (`RequestSimulcastRid`) を実行しても受信側の解像度が切り替わらない事象を調査し、C++ SDK 側で対処が必要な場合は修正する。E2E テストで確認できる見込みがある。

## 現状

### 発生事象

- Ubuntu 24.04.1 の Intel VPL 環境 (Intel Core Ultra 5 125H、Intel iHD driver 24.1.0) で AV1 サイマルキャストを送信し、受信側の受信ストリームを切り替える API を実行しても、受信側の解像度が切り替わらない
- 事象を最初に確認した際に使用した API は非推奨の `RequestRtpStream` (`Sora_20201005.RequestRtpStream`) である。この機能は現行の Sora (2025.12 以降) で正式 API の `RequestSimulcastRid` (`Sora_20251217.RequestSimulcastRid`) に置き換わっており、`RequestRtpStream` は 2027 年 12 月リリース予定の Sora で廃止される。本 issue の対応では `RequestSimulcastRid` を使う
- Sora C++ SDK は `SoraSignalingConfig::simulcast_request_rid` を持ち、`src/sora_signaling.cpp` の connect メッセージで接続時に `simulcast_request_rid` を Sora へ送る。しかし接続後に受信する rid を切り替える手段は Sora サーバーの HTTP API (`api_port`、既定 3000) のみであり、SDK には該当 API を呼び出す仕組みも切り替え結果を確認する仕組みもない
- `examples/sumomo` に `simulcast_request_rid` を指定するオプションはない (サイマルキャスト関連では `--simulcast` のみ)
- `e2e-test/` に Sora の HTTP API を実行して解像度を確認するテストはない。`e2e-test/conftest.py` の `SoraSettings` は signaling URL / channel ID prefix / secret key / channel ID / metadata のみで、Sora の API を呼ぶ仕組みを持っていない
- 同一環境の AV1 受信で映像が緑になる事象は、`intel-media-va-driver-non-free` の導入で解消済みであり、本 issue は rid 切り替えに限定する

### 再現手順

1. `intel-media-va-driver-non-free` を導入した Ubuntu 24.04 の Intel VPL 環境で、送信側の sumomo を起動する (`--role sendonly --simulcast true --video-codec-type AV1 --av1-encoder intel_vpl --resolution 960x540 --video-bit-rate 3000`)
2. 同じチャネル ID で受信側の sumomo を起動する (`--role recvonly --simulcast true --video-codec-type AV1`)。受信側が `simulcast_request_rid` を指定しない場合、Sora は `r0` を配信するため、受信側の `inbound-rtp` は 240x128 になる
3. 受信側が映像を受信し始めたら、Sora の HTTP API に `RequestSimulcastRid` (POST /、ヘッダー `x-sora-target: Sora_20251217.RequestSimulcastRid`) を `channel_id` / `receiver_connection_id` (受信側) / `sender_connection_id` (送信側) / `rid: "r2"` で送る。接続 ID は `ListChannelConnections` (`Sora_20201013.ListChannelConnections`) で `client_id` から特定する
4. 受信側の `http://<host>:<port>/stats` から `inbound-rtp` の `frameWidth` / `frameHeight` を確認する

期待する挙動は `r0` の 240x128 から `r2` の 960x528 へ変化することである。

### 実行ログと環境

- 実行ログ: https://gist.github.com/torikizi/d7ce636b1970091e192f594d713ed258
  - 2024-10-09 収集、Sora C++ SDK 2024.8.0-canary.13 / libwebrtc M129.6668 / Sora 2024.2.0-canary.30 のログである
  - このログは AV1 サイマルキャストの接続と送受信の確認用で、Sora の API の実行や解像度の切り替えの成否は含まない

## 調査結果

原因を特定した。SDK 側の修正は 2025.5.0 で既に入っており、現行の `develop` では再現しない見込みである。残作業は E2E テストでの確認になる。実機が Intel VPL の環境を用意できないため、SDK と libwebrtc と `intel/vpl-gpu-rt` のソースから確認した。

### 原因

送信側 SDK の Intel VPL AV1 エンコーダーが、AV1 の Dependency Descriptor (DD) RTP ヘッダー拡張を付与していなかったことである。

- 修正前の `VplVideoEncoderImpl::Encode` (`src/hwenc_vpl/vpl_video_encoder.cpp`) には AV1 の分岐が存在せず、`CodecSpecificInfo` が既定値のまま `EncodedImageCallback::OnEncodedImage` に渡されていた。このため `generic_frame_info` と `template_structure` が設定されず、DD が RTP に載らない。`VplVideoEncoderImpl::InitEncode` も AV1 用の `webrtc::ScalableVideoController` を生成していなかった
- 同じ事象が momo の issue として報告されている。VPL 環境の AV1 で「後から参加したクライアントで映像を受信できない」「サイマルキャスト時に rid の切り替えをしても映像が切り替わらない」の両方が再現しており、本 issue と症状が一致する
  - https://github.com/shiguredo/momo/issues/357 (2024-09-12 起票、2025-08-07 closed)
- この issue は Sora C++ SDK 2025.5.0 の `[FIX] Intel VPL の AV1 エンコーダーで Dependency Descriptor RTP ヘッダー拡張が追加されない問題を修正` で解消している (`CHANGES.md`、commit `3c37c35`、sora-cpp-sdk の PR #224)。AV1 用の SVC コントローラーを Intel VPL にも追加し、`generic_frame_info` と `template_structure` を設定するようにしたものである
- 同時期に `[FIX] Intel VPL の VP9 エンコーダーでキーフレーム要求が機能しない問題を修正` も入っている。VP9 は別原因 (`mfxEncodeCtrl::FrameType` に `MFX_FRAMETYPE_IDR` / `MFX_FRAMETYPE_REF` を同時に設定すると vpl-gpu-rt が `MFX_FRAMETYPE_P` に書き換える) である

### なぜ DD の欠落が「解像度が切り替わらない」になるか

1. `inbound-rtp` の `frameWidth` / `frameHeight` は「最後にデコードされ描画されたフレーム」の解像度であり、切替時にリセットする仕組みがない (`ReceiveStatisticsProxy::OnRenderedFrame`、`VideoReceiveStream2::OnFrame`)。したがって「解像度が切り替わらない」は「新レイヤーのフレームが 1 枚もデコードされていない」を意味する
2. AV1 の受信側は、フレームのキーフレーム性と依存関係を DD から判定する (`RtpVideoStreamReceiver2::ParseGenericDependenciesExtension`)。DD に構造 (`attached_structure`) が付いたフレームだけがキーフレーム扱いになり、付いていなければ delta 扱いになる。また既知の構造と突き合わせられない DD のパケットは破棄または保留されるため、途中からレイヤーの受信を始める場合は構造を添付したキーフレームが必要になる
3. DD が無い場合、AV1 は `RtpFrameReferenceFinderImpl::ManageFrame` の既定経路 (`RtpSeqNumOnlyRefFinder`) に落ちる。この経路は AV1 ペイロードの N ビットによるキーフレーム検出と RTP シーケンス番号の連続性に依存するため、SFU が途中からレイヤーの転送を開始する状況 (rid 切替直後、後から参加した受信側) ではデコード開始点を安定して得られない (推定。libwebrtc の該当経路を修正前のバージョンで実行して確認したものではない)
4. その結果、受信側は新レイヤーのフレームを破棄し続け (キーフレーム待ちのまま)、統計は切替前の 240x128 のままになる

### 送信側の現行実装

- `VplVideoEncoderImpl::InitEncode` は AV1 のとき `webrtc::CreateScalabilityStructure` で SVC コントローラーを生成する
- `VplVideoEncoderImpl::Encode` は AV1 のとき `codec_specific.generic_frame_info` を設定し、キーフレームのときだけ `codec_specific.template_structure` と `resolutions` を設定する
- 送信側 libwebrtc は `RtpVideoSender::SendVideo` の `IsFirstFrameOfACodedVideoSequence` の判定で `template_structure` を DD に添付する

### 否定した原因

- VPL の AV1 エンコーダーが PLI によるキーフレーム要求に応答しない、という説は否定できる。`intel/vpl-gpu-rt` の AV1 エンコーダー実装 (`av1ehw_base_*`) には VP9 の `CheckAndFixCtrl` に相当するフレームタイプの書き換えが存在せず、`MFX_FRAMETYPE_I | MFX_FRAMETYPE_IDR | MFX_FRAMETYPE_REF` は KEY_FRAME として扱われる。また I フレームには sequence header OBU が付与される (`General::InitTask`、`MapMfxFrameTypeToSpec` 相当の処理)。`intel-onevpl-24.1.0` でも論理は同一である。`src/hwenc_vpl/vpl_video_encoder.cpp` の 3 フラグ設定は AV1 に対しては正しい
- レイヤー解像度そのものは正しい。`AlignedEncoderAdapter` が 16 の倍数へ切り捨てるため、960x540 の 3 レイヤーは 240x128 / 480x256 / 960x528 になる。`e2e-test/test_sumomo_intel_vpl.py` の `test_simulcast` の期待値と一致する
- 送信側の `SimulcastEncoderAdapter` は `GetEncoderInfo()` の `supports_simulcast` が偽のため常に multi-encoder モードになり、レイヤーごとに個別の VPL エンコーダーが生成される。レイヤー単位のキーフレーム要求は `EncoderRtcpFeedback::OnReceivedIntraFrameRequest` から全レイヤーへの `SendKeyFrame()` として渡る
- `issues/0076-bug-fix-vpl-av1-h265-two-stream-simulcast.md` のストリーム数 2 本以下の事象は、VPL が扱える最小解像度 (`issues/pending/0100-bug-fix-vpl-av1-small-resolution.md`) による初期化失敗であり、本 issue とは別原因である

### 切り分け方法

- 受信側 stats の `inbound-rtp` の `ssrc` が切替前後で変わるかを確認する。Sora が送信側の SSRC をそのまま転送する場合、libwebrtc は未知の SSRC のパケットで受信ストリームとデコーダーを作り直す (`WebRtcVideoReceiveChannel::MaybeCreateDefaultReceiveStream` と `ReCreateDefaultReceiveStream`)。SSRC が変わらない場合は同一デコーダーのまま解像度だけが変わる
- `framesReceived` と `framesDecoded` の増分で「パケットが届いていない」のか「デコードできていない」のかを分ける
- 送信側の rid ごとの `framesEncoded` / `keyFramesEncoded` と `Key Frame Generated` のログで、要求されたキーフレームが出ているかを確認する
- シグナリング通知 `simulcast.switched` の `trigger` と `current_rid` で Sora 側が切り替えたことを確認する。切り替えているのに解像度が変わらない場合は受信側の問題と判断できる。必要なら `RequestKeyFrame` API で手動 PLI を送って切り分ける
- E2E テストでは、SSRC が変わると `inbound-rtp` のエントリ自体が入れ替わるため、単一の `inbound-rtp` を固定で見る書き方にしないこと

### 残っている懸念

- 受信側の Intel VPL デコーダーの解像度変更処理 (`VplVideoDecoderImpl::Decode`) に既知の問題がある。`MFX_ERR_MORE_DATA` 以外で `syncp` が無いときに `continue` するため `MFX_ERR_INCOMPATIBLE_VIDEO_PARAM` などで無限ループする (`issues/pending/0090-bug-fix-vpl-decoder-incompatible-video-param-loop.md`)。また `VPL_CHECK_RESULT` が正の警告 `MFX_WRN_VIDEO_PARAM_CHANGED` をエラー扱いするため、解像度変更を検出したフレームを捨て、`SyncOperation` を呼ばないことでサーフェスが解放されない
- 上記は SSRC が変わらず同一デコーダーで解像度が変わる構成のときに本 issue と同じ症状になりうる。E2E テストで切り替えが確認できない場合の次の調査対象とする

## 設計方針

- E2E テストで Sora の HTTP API `RequestSimulcastRid` を実行し、受信側の `inbound-rtp` の `frameWidth` / `frameHeight` が切り替わるかを確認できるようにする。E2E の設定に Sora API の URL (既定の `api_port` は 3000) を追加する必要がある。Sora の HTTP API に認証機能は現在のドキュメントに記載がないため、E2E で用意するのは API を呼べる URL と、`ListChannelConnections` で接続 ID を取得する手段であり、API の認証は Sora 側の設定に依存する
- 切り替わらない場合、Sora が切り替え時に受信側へ通知を行っているかも含め、送信側・受信側のどちらに原因があるかを切り分ける
- Sora 側の挙動や libwebrtc 側の対応が必要かを確認する
- 再現に使った環境 (Ubuntu 24.04.1 / Intel Core Ultra / iHD driver のバージョン) と実行ログを issue に記録する

## 完了条件

- E2E テストで `RequestSimulcastRid` による解像度の切り替えが確認できること。テストは `e2e-test/test_sumomo_intel_vpl.py` に追加し、`INTEL_VPL=1` で実行できること。送信 960x540 の AV1 では、受信側の `inbound-rtp` が `r0` の 240x128 から `r2` の 960x528 へ変化することを確認する
- 切り替わらない場合、原因が特定され、SDK 側の修正または Sora / libwebrtc 側の対応要否が判断されていること
- 通常のサイマルキャスト送信に回帰がないこと (既存の `e2e-test/test_sumomo_intel_vpl.py` の `test_simulcast` が通ること)
