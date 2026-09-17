# offer の encodings の priority / networkPriority を RtpEncodingParameters に反映する

- Created: 2026-09-10
- Completed: 2026-09-18
- Branch: feature/add-simulcast-encoding-priority
- Polished: 2026-09-14

## 目的

Sora 2026.1.0 で、offer メッセージの `encodings`（サイマルキャストの `simulcast_encodings` の払い出し）に `priority` と `networkPriority` を指定できるようになった。Sora C++ SDK は現在これらの値を受け取っても読み捨てているため、Sora 側で指定した優先度が送信側のエンコーディングパラメーターに反映されない。`webrtc::RtpEncodingParameters` の対応するフィールドへ反映できるようにする。

## 現状

- `src/sora_signaling.cpp` の `SoraSignaling::OnRead` の `type == "offer"` 分岐で、`offer_config_.simulcast` が true の場合のみ `encodings` 配列の各要素を `webrtc::RtpEncodingParameters` へ変換している。対応済みの項目は `rid` / `maxBitrate` / `minBitrate` / `scaleResolutionDownBy` / `maxFramerate` / `active` / `adaptivePtime` / `scalabilityMode` / `scaleResolutionDownTo` で、`priority` と `networkPriority` は変換していない
- 変換した `RtpEncodingParameters` は `SoraSignaling::SetEncodingParameters` で映像トランシーバーの sender へ `RtpSenderInterface::SetParameters` により設定し、`encodings_` に保持する。`SoraSignaling::ResetEncodingParameters` は `encodings_` を使って sender へ再設定する
- `webrtc::RtpEncodingParameters`（libwebrtc m154.8037.1.1）には以下のフィールドがある
  - `double bitrate_priority = kDefaultBitratePriority;`（ `kDefaultBitratePriority` は 1.0 ）
  - `Priority network_priority = Priority::kLow;`

## 設計方針

offer の `encodings` の各要素にある `priority` と `networkPriority` を、`webrtc::RtpEncodingParameters` の対応するフィールドへ変換する。値が含まれない場合は従来どおり何も設定しない。

| offer の値 | W3C での対応 | RtpEncodingParameters |
| --- | --- | --- |
| `priority` | `RTCRtpEncodingParameters.priority` | `bitrate_priority`（double） |
| `networkPriority` | `RTCRtpEncodingParameters.networkPriority` | `network_priority`（`webrtc::Priority`） |

### priority → bitrate_priority

libwebrtc の `bitrate_priority` は W3C の `RTCRtpEncodingParameters.priority` に対応する。値の対応は libwebrtc の `api/rtp_parameters.h` のコメントに従う。

| offer の値 | bitrate_priority |
| --- | --- |
| `very-low` | 0.5 |
| `low` | 1.0 |
| `medium` | 2.0 |
| `high` | 4.0 |

### networkPriority → network_priority

libwebrtc の `network_priority` は W3C の `RTCRtpEncodingParameters.networkPriority` に対応する。値の対応は以下のとおり。

| offer の値 | network_priority |
| --- | --- |
| `very-low` | `webrtc::Priority::kVeryLow` |
| `low` | `webrtc::Priority::kLow` |
| `medium` | `webrtc::Priority::kMedium` |
| `high` | `webrtc::Priority::kHigh` |

### 不正値の扱い

上記 4 値のいずれでもない文字列（空文字列を含む）の場合は、警告ログを出力した上でそのフィールドを設定せず、`webrtc::RtpEncodingParameters` のデフォルト値を維持する。

### 確認用ログ

`SoraSignaling::SetEncodingParameters` と `SoraSignaling::ResetEncodingParameters` のエンコーディング情報ログに `bitrate_priority` と `network_priority` を追加し、実際に反映された値を確認できるようにする。

## 懸念

- Sora ドキュメントでは `priority` と `networkPriority` はどちらも「この設定は Chrome でしか利用できません」と注記されている。また Sora iOS SDK では `priority` の反映が pending になっている。libwebrtc の `bitrate_priority` は、現在はエンコーディングごとではなく RTP sender 全体に対して最初のエンコーディングの値を使って適用される（`api/rtp_parameters.h` のコメントに "Currently this is implemented for the entire rtp sender by using the value of the first encoding parameter." とあり、エンコーディングごとの適用は TODO）。ネイティブ SDK での実効性は不透明なため、実装時にログと実機の動作で確認する
- W3C の `RTCRtpEncodingParameters.networkPriority` は生成パケットの DSCP マーキングのみに影響する。DSCP マーキングを有効にするには `webrtc::PeerConnectionInterface::RTCConfiguration` の `enable_dscp`（ `set_dscp(true)` ）が必要だが、Sora C++ SDK では設定していないため、現状のままだと `network_priority` を反映しても送信パケットの DSCP 値は変わらない。本 issue は Sora から受け取った値を `RtpEncodingParameters` へ反映するところまでをスコープとし、DSCP マーキングの有効化は本 issue のスコープ外とする

## 完了条件

- offer の `encodings` に `priority` が含まれる場合、`bitrate_priority` に反映されること
- offer の `encodings` に `networkPriority` が含まれる場合、`network_priority` に反映されること
- `priority` / `networkPriority` が含まれない場合は従来どおりの挙動を維持すること
- 4 値以外の値の場合は警告ログを出力し、該当フィールドを変更しないこと
- 変更履歴（ `CHANGES.md` ）の `## develop` に `[ADD]` エントリを追記していること

### 変更履歴の書き方サンプル

```markdown
- [ADD] offer の encodings の priority と networkPriority を RtpEncodingParameters に反映する
  - @<担当者>
```

## テスト方針

- `encodings` の変換処理には現状ユニットテストがないため、新規ユニットテストは追加しない
- Sora の認証ウェブフックまたは `sora.conf` の `simulcast_encodings_file` で `priority` / `networkPriority` を指定した状態の Sora へ接続し、追加したログで `bitrate_priority` / `network_priority` が反映されることを確認する
- `priority` / `networkPriority` を指定しない場合に従来と同じ値になることを確認する

## 解決方法

`src/sora_signaling.cpp` の `SoraSignaling::OnRead` の `type == "offer"` 分岐で、`encodings` の各要素の `priority` と `networkPriority` を `webrtc::RtpEncodingParameters` へ反映するようにした。

- `encodings` の変換ループの最後（`scaleResolutionDownTo` の処理の後）で、`priority` がある場合は `very-low` / `low` / `medium` / `high` を `bitrate_priority` の `0.5` / `1.0` / `2.0` / `4.0` に変換して設定する。この重み付けは libwebrtc の `api/rtp_parameters.h` のコメント（およびその元である W3C の `RTCRtpEncodingParameters.priority`）に合わせている
- `networkPriority` がある場合は `very-low` / `low` / `medium` / `high` を `webrtc::Priority` の `kVeryLow` / `kLow` / `kMedium` / `kHigh` に変換して `network_priority` に設定する
- キーがない場合はフィールドに触れず、libwebrtc のデフォルト値（`bitrate_priority` は `kDefaultBitratePriority`、`network_priority` は `Priority::kLow`）を維持する
- 4 値のいずれでもない文字列（空文字列を含む）の場合は `RTC_LOG(LS_WARNING)` で警告を出力し、そのフィールドは変更しない
- `SoraSignaling::SetEncodingParameters` と `SoraSignaling::ResetEncodingParameters` のエンコーディング情報ログに `bitrate_priority` と `network_priority` を追加した
- `CHANGES.md` の `## develop` に `[ADD]` エントリを追記した

DSCP マーキングの有効化（`webrtc::PeerConnectionInterface::RTCConfiguration` の `enable_dscp`）は本 issue のスコープ外のため実施していない。

確認:

- `python3 run.py build ubuntu-24.04_x86_64 --disable-cuda` が通ることを確認した（CUDA を有効にしたビルドは、この環境の `/usr/include/cuda.h` と `include/sora/dyn/cuda.h` の `cuCtxCreate` の引数が食い違うため通らない。本変更とは無関係の既存の問題）
- `python3 run.py format`（`clang-format-23` が選択される）で差分が出ないことを確認した
- 実 Sora（2026.1.2）へ sumomo をサイマルキャスト有効で接続し、追加したログで `bitrate_priority=1` / `network_priority=1`（どちらも未指定時のデフォルト値）が 3 層分反映されることを確認した
- `test_sumomo_basic.py::test_sumomo_simulcast` が通ることを確認した
- `priority` / `networkPriority` を実際に指定した offer での確認は、Sora 側に `simulcast_encodings_file` などの設定が必要なため未実施
