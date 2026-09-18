# adaptivePtime を SDK オプションで音声トラックに適用する

- Created: 2026-09-10
- Completed: 2026-09-18
- Branch: feature/add-adaptive-ptime
- Polished: 2026-09-14

## 目的

音声送信の adaptivePtime（適応的パケット化時間）を SDK の設定で有効化できるようにする。

adaptivePtime は W3C の `RTCRtpEncodingParameters.adaptivePtime` に対応する音声向けパラメータで、libwebrtc では audio の voice engine (`media/engine/webrtc_voice_engine.cc`) だけが `webrtc::RtpEncodingParameters::adaptive_ptime` を参照する。映像側では参照されない。サイマルキャストの有無に関係なく、SDK の設定で音声トラックに適用できるようにする。

## 現状

- `include/sora/sora_signaling.h` の `SoraSignalingConfig` に adaptivePtime を設定する項目がない
- `src/sora_signaling.cpp` の `SoraSignaling::OnRead` の `type == "offer"` 分岐で、`offer_config_.simulcast` が true かつ offer に `encodings` がある場合のみ、offer の `encodings[]` の `adaptivePtime` を `webrtc::RtpEncodingParameters::adaptive_ptime` に変換している
- 変換した encodings は `SoraSignaling::SetEncodingParameters` で `video_mid_` の video sender に `RtpSenderInterface::SetParameters` で設定している
- offer の `encodings[]` の `adaptivePtime` は Sora の `simulcast_encodings` の項目で、sora-doc のサイマルキャスト機能 (https://sora-doc.shiguredo.jp/SIMULCAST) の「映像のエンコーディングパラメーターのカスタマイズ」に `priority` / `networkPriority` などと並んで記載されている。Sora が払い出した値をそのまま sender に反映するのが SDK の役割なので、この変換は削除しない
- libwebrtc で `adaptive_ptime` を参照するのは audio の voice engine だけなので、video sender に設定しても音声の adaptivePtime は有効にならない
- `audio_mid_` は保持しているが、audio sender の encoding パラメータを設定する経路がない

## 設計方針

- `SoraSignalingConfig` に `std::optional<bool> audio_adaptive_ptime;` を追加する。`degradation_preference` と同じく optional で未設定を表現する
- `audio_adaptive_ptime` に値が設定されている場合、`audio_mid_` から audio transceiver を引き、その sender の `RtpParameters::encodings` の各要素の `adaptive_ptime` にその値を設定して `SetParameters` する。適用場所は `SetDegradationPreference` を呼んでいる 3 つの `SessionDescription::CreateAnswer` コールバック（WebSocket 経由の offer、WebSocket 経由の update / re-offer、DataChannel 経由の re-offer）すべてとし、それぞれ `SetDegradationPreference` と同じ並びに追加する
  - audio transceiver が見つからない場合（音声トラックが追加されていない場合など）は `SetDegradationPreference` と同様にエラーログを出力して何もしない
  - `SetParameters` に失敗した場合はエラーログを出力する
- offer の `encodings[].adaptivePtime` の変換は変更しない。Sora の `simulcast_encodings` に定義されている項目であり、offer で指定された値を video sender に反映する従来の挙動を維持する
- `audio_adaptive_ptime` が未設定の場合は audio sender の `adaptive_ptime` を変更しない（後方互換）
- 変更履歴（`CHANGES.md`）の `## develop` に `[ADD]` エントリを追記する。担当者行は変更内容より 2 文字分インデントを下げる

  ```markdown
  - [ADD] adaptivePtime を SDK オプションで音声トラックに適用できるようにする
    - @<担当者>
  ```

## 完了条件

- `audio_adaptive_ptime` に true を設定すると audio sender の encoding の `adaptive_ptime` が true になること
- `audio_adaptive_ptime` に false を設定すると audio sender の encoding の `adaptive_ptime` が false になること
- `audio_adaptive_ptime` が未設定の場合は audio sender の `adaptive_ptime` が変更されず従来どおりの挙動になること
- `audio_adaptive_ptime` の設定が video sender に波及しないこと
- offer の `encodings[].adaptivePtime` を video sender に反映する従来の挙動が維持されていること
- `CHANGES.md` の `## develop` に `[ADD]` が追記されていること

## テスト方針

- audio sender の `sender->GetParameters()` の `encodings[].adaptive_ptime` が、`audio_adaptive_ptime` の true / false / 未設定に応じて期待どおりの値になることを確認する
- `audio_adaptive_ptime` を設定しても video sender の `encodings[].adaptive_ptime` が変化しないことを確認する
- 既存の offer 変換・適用処理にユニットテストがないため、`test/e2e.cpp` と同様に実 Sora サーバへ接続する Catch2 テストで audio track を追加して接続し、audio sender と video sender の `RtpSenderInterface::GetParameters()` を確認する。`test/e2e.cpp` の既存ケースは `config.audio = false` のため、audio 送信の検証には audio track の追加が必要
- offer の `encodings[].adaptivePtime` が video sender に反映されることの確認は、Sora 側に `simulcast_encodings` の設定が必要なため、この issue ではテストしない

## 解決方法

`SoraSignalingConfig` に `audio_adaptive_ptime` を追加し、値が設定されている場合は audio sender の `webrtc::RtpEncodingParameters::adaptive_ptime` にその値を設定するようにした。

- `SoraSignaling::SetAdaptivePtime` を追加した。`audio_mid_` から audio transceiver を引き、sender の `RtpParameters::encodings` の各要素に指定された値を設定して `SetParameters` する。audio transceiver が見つからない場合と `SetParameters` に失敗した場合はエラーログを出力する
- 適用場所は `SetDegradationPreference` を呼んでいる 3 つの `SessionDescription::CreateAnswer` コールバック（WebSocket 経由の offer、WebSocket 経由の update / re-offer、DataChannel 経由の re-offer）とした
- offer の `encodings[].adaptivePtime` を video sender に反映する従来の挙動は維持した。`adaptivePtime` は Sora の `simulcast_encodings` の項目であり、Sora が払い出した値をそのまま sender に反映する
- `CHANGES.md` の `## develop` に `[ADD]` エントリを追記した
- `test/audio_adaptive_ptime.cpp` を追加し、実 Sora へ接続して audio sender の `adaptive_ptime` が true / false / 未設定で期待どおりになることと、`audio_adaptive_ptime` の設定が video sender に波及しないことを確認した
- `run.py` の `--run-e2e-test` で `audio_adaptive_ptime` をビルド・実行するようにした

確認:

- `python3 run.py build ubuntu-24.04_x86_64 --test --run-e2e-test --disable-cuda` が通り、`audio_adaptive_ptime` を含む全てのテストが成功することを確認した
- 実 Sora へ接続し、`SetAdaptivePtime: ssrc=... adaptive_ptime=true` と `adaptive_ptime=false` が出力され、未設定の場合は呼ばれないことを確認した
- `python3 run.py format` で差分が出ないことを確認した
