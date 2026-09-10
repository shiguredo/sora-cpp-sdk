# offer の encodings の priority / networkPriority を RtpEncodingParameters に反映する

- Created: 2026-09-10
- Completed: {YYYY-MM-DD}
- Branch: feature/add-simulcast-encoding-priority
- Polished: {YYYY-MM-DD}

## 目的

Sora 2026.1.0 で、offer メッセージの `encodings`（サイマルキャストの `simulcast_encodings` の払い出し）に `priority` と `networkPriority` を指定できるようになった。Sora C++ SDK は現在これらの値を受け取っても読み捨てているため、Sora 側で指定した優先度が送信側のエンコーディングパラメーターに反映されない。`webrtc::RtpEncodingParameters` の対応するフィールドへ反映できるようにする。

## 現状

- `src/sora_signaling.cpp` の `SoraSignaling::OnRead` の `type == "offer"` 分岐で、`encodings` 配列の各要素を `webrtc::RtpEncodingParameters` へ変換している。対応済みの項目は `rid` / `maxBitrate` / `minBitrate` / `scaleResolutionDownBy` / `maxFramerate` / `active` / `adaptivePtime` / `scalabilityMode` / `scaleResolutionDownTo` で、`priority` と `networkPriority` は変換していない
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

- Sora ドキュメントでは `priority` と `networkPriority` はどちらも「この設定は Chrome でしか利用できません」と注記されている。また Sora iOS SDK では `priority` の反映が pending になっている。libwebrtc の `bitrate_priority` はサイマルキャストのビットレート配分で参照されるが、ネイティブ SDK での実効性は不透明なため、実装時にログと実機の動作で確認する
- `network_priority` による DSCP マーキングを有効にするには `webrtc::PeerConnectionInterface::RTCConfiguration` の `enable_dscp`（ `set_dscp(true)` ）が必要だが、Sora C++ SDK では設定していない。DSCP マーキングの有効化は本 issue のスコープ外とする

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
