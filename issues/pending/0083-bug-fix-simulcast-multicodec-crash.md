# サイマルキャストマルチコーデックで r0 を active:false にするとクラッシュする

- Created: 2026-09-11
- Completed: {YYYY-MM-DD}
- Branch: feature/fix-simulcast-multicodec-crash
- Polished: {YYYY-MM-DD}

## 目的

サイマルキャストマルチコーデック利用時に、rid ごとに異なるコーデックを指定した状態で r0 を `active: false` にすると、Sora C++ SDK で接続した際にクラッシュする。接続不能になる致命的な不具合であるため、クラッシュが発生しないことを確認する。

## 現状

- 認証ウェブフックで設定したケースと `sora.conf` でファイルを設定したケースの両方で、C++ SDK (sumomo) で接続した際にクラッシュする
- r0 と r1 以降が同一コーデックの場合は再現しないように見える
- クラッシュログは `[bad_variant_access.cc : 44] RAW: Bad variant access`
- 検証環境: macOS、sumomo を `--simulcast true --simulcast-multicodec true` で実行

再現に使った simulcast_encodings は r0 のみ `active: false`:

```
{"rid": "r0", "active": false, "scalabilityMode": "L1T1", "scaleResolutionDownBy": 4.0, "maxFramerate": 10.0},
{"rid": "r1", "active": true,  "scalabilityMode": "L1T1", "scaleResolutionDownBy": 2.0, "maxFramerate": 30.0},
{"rid": "r2", "active": true,  "scalabilityMode": "L1T1", "scaleResolutionDownBy": 1.0, "maxFramerate": 30.0}
```

再現に使った simulcast_codecs:

```
[
  {"rid": "r0", "codec_type": "AV1"},
  {"rid": "r1", "codec_type": "H264"},
  {"rid": "r2", "codec_type": "H264"}
]
```

- 現在の `develop` にはサイマルキャストマルチコーデックの実装がない。`src/sora_signaling.cpp` の offer の `encodings` 解析は `rid` / `active` / `scaleResolutionDownBy` / `maxFramerate` などを扱うが、per-rid の `codec` (`webrtc::RtpEncodingParameters::codec`) は扱っていない (`std::optional<RtpCodec> codec;` はコメントアウトされている)
- サイマルキャストマルチコーデックの実装は `feature/multi-codec-simulcast` ブランチにあり、`develop` にはマージされていない。libwebrtc の upstream にもこの機能は入っていない
- 他 SDK (Android / iOS / C) でも同じ事象が確認されており、専用の libwebrtc ビルド (`simulcast-multi-codec` タグ) で解消される可能性が示されている

## 設計方針

- サイマルキャストマルチコーデックが利用できる状態になったら、r0 を `active: false` にした構成で接続し、クラッシュの有無を確認する
- 再現する場合は、無効化された rid に対応するコーデックの扱いを `src/sora_signaling.cpp` の `SetEncodingParameters` と `webrtc::RtpEncodingParameters` の `codec` の処理から切り分ける
- 再現しない場合は、どの libwebrtc バージョンで解消されたかを確認してクローズする
- `feature/multi-codec-simulcast` ブランチを現行の libwebrtc に追従させる必要があるかは別途判断する

## 完了条件

- 現行の libwebrtc で再現確認が行われていること
- 再現する場合、rid ごとに異なるコーデックを指定し r0 を `active: false` にした構成でクラッシュしないこと
- 通常のサイマルキャスト (同一コーデック) に回帰がないこと

## Pending 理由

- サイマルキャストマルチコーデックの実装が `develop` にマージされておらず、libwebrtc の upstream にも入っていないため、現行の Sora C++ SDK では再現確認ができない
- クラッシュが確認されたのは専用の libwebrtc ビルド (`feature/multi-codec-simulcast` ブランチ) であり、そのブランチは libwebrtc m138 対応で止まっている
- 再現確認と解消確認ができる状態になるまで、対応方針 (SDK 側の修正か libwebrtc 側の修正か) が決められない

## Pending 解除条件

- サイマルキャストマルチコーデックが現行の libwebrtc / Sora C++ SDK で利用できる状態になること
- その状態で r0 を `active: false` にした構成の再現確認ができること
