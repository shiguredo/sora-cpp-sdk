# SoraVideoDecoderFactory / SoraVideoEncoderFactory の formats_ 並行アクセスによる abort を修正する

- Priority: High
- Created: 2026-05-27
- Model: Opus 4.7
- Branch: feature/fix-video-factory-formats-data-race-abort

## 目的

受信トラックのデコーダ生成と再ネゴシエーションが並行で走る状況で、`SoraVideoDecoderFactory::Create()` がプロセスごと abort (SIGABRT / exit code 134) する事象を解消する。

`SoraVideoDecoderFactory` / `SoraVideoEncoderFactory` は `webrtc::VideoDecoderFactory` / `webrtc::VideoEncoderFactory` の実装であり、`GetSupportedFormats()` と `Create()` は libwebrtc が内部スレッドから任意のタイミングで呼ぶ。両メソッドが同一 factory の `formats_` メンバを未同期で並行アクセスしており、マルチコーデックかつ参加者が出入りする通常利用 (多人数セッション) で abort を踏む。SDK 利用側にはこの並行性を制御する手段がないため、安定性に対する影響が大きい。

## 優先度根拠

High とする。

- abort はプロセスごと落ちる (SIGABRT) ため、SDK を組み込んだアプリケーションを巻き込んで停止する。C++ 例外として捕捉できず、利用側での回避手段がない。
- 「受信中は再ネゴシエーションしない」以外に避ける道がなく、それは多人数セッションでは非現実的な制約。通常利用の範囲内で発生する。
- 後述の native スタックの crash 位置 (`Create+0x41c`) が、異なるコーデック・異なる接続タイミングの複数の独立した観測で一致しており、単発の偶発事象ではなく構造的なバグである。

## 現状

### 失敗内容

`video_codec_type` の指定有無や特定コーデックに依存せず、デコーダ生成経路で abort する。crashing thread の native スタックの中核フレームは以下で共通している。

```
abort
__libcpp_verbose_abort  (ハードニング有効の libc++ による境界チェック)
sora::SoraVideoDecoderFactory::Create(const webrtc::Environment&, const webrtc::SdpVideoFormat&)+0x41c
... (libwebrtc の worker / decoder スレッド)
```

複数の独立した観測 (異なるコーデック・異なる接続タイミング) で `Create(...)+0x41c` まで一致しており、同一バグであることを確認している。

### 根本原因

`SoraVideoDecoderFactory` の `formats_` キャッシュに対する未同期な並行アクセス。

- `include/sora/sora_video_decoder_factory.h:70` で `mutable std::vector<std::vector<webrtc::SdpVideoFormat>> formats_;` と定義されており、保護する mutex を持たない。
- `GetSupportedFormats() const` (`src/sora_video_decoder_factory.cpp:50`) は呼ばれるたびに `formats_.clear()` (51 行目) してから decoder ごとに `formats_.push_back(...)` (75 行目) で作り直す。クリア中〜再構築途中は `formats_` のサイズが 0〜decoder 数未満の不整合状態になり、`push_back` による再確保で要素のアドレスも移動する。
- `Create()` (`src/sora_video_decoder_factory.cpp:81`) は `std::vector<webrtc::SdpVideoFormat> supported_formats = formats_[n++];` (95 行目) で添字アクセスし、`formats_.size() == config_.decoders.size()` を暗黙の前提にしている。
- この 2 メソッドは libwebrtc が別々の内部スレッドから呼ぶ。`GetSupportedFormats` は (再) ネゴシエーション時 (signaling thread)、`Create` は受信トラックのデコーダ生成時 (worker / decoder thread)。両者が同一 factory の `formats_` を未同期で並行アクセスするため、`Create` が clear / 再構築途中の `formats_` を読むと以下が起こる。
  - `n >= formats_.size()` の状態で `operator[]` を呼ぶと、ハードニング有効の libc++ (libwebrtc 同梱のビルドで有効) では境界チェックが `abort()` を呼ぶ。これが `Create+0x41c` → `abort` に対応する。
  - 仮に境界チェックが無くても、再確保で移動・破棄された記憶域を読む UB であり、いずれにせよ正しく動作しない。

### エンコーダ側にも同一の欠陥がある

`SoraVideoEncoderFactory` も全く同じパターンを持つ。

- `include/sora/sora_video_encoder_factory.h:104` で `mutable std::vector<std::vector<webrtc::SdpVideoFormat>> formats_;` を保護なしで保持する。
- `GetSupportedFormats() const` (`src/sora_video_encoder_factory.cpp:61`) が `formats_.clear()` (62 行目) → `formats_.push_back(...)` (86 行目) で再構築する。
- `CreateInternalVideoEncoder()` (`src/sora_video_encoder_factory.cpp:91`) が `formats_[n++]` (111 行目) で添字アクセスする。さらに `if (formats_.empty()) { GetSupportedFormats(); }` (96-98 行目) で遅延初期化しており、この経路自体も `formats_` を書き換える。

abort が確認されているのはデコーダ側だが、共有可変状態を未同期で添字前提に読むという欠陥の構造はエンコーダ側と同一である。デコーダのみ直してもエンコーダ側に同種の潜在 abort が残るため、同じ修正方針で両方を根絶する。なお、エンコーダはサイマルキャスト用の内部 factory (`internal_encoder_factory_`) を持ち、危険な添字読みは内部 factory の `CreateInternalVideoEncoder` 側で起きる構造のため、並行経路の具体的な成立条件は実装時に確認すること。後述の案 1 を採れば共有状態そのものが消えるため、経路の有無に関わらず正しく解消できる。

### 利用側で回避できるか

回避できない。`GetSupportedFormats` / `Create` は `webrtc::VideoDecoderFactory` / `webrtc::VideoEncoderFactory` の仮想関数で、SDK 利用者が直接呼ぶものではなく libwebrtc が内部スレッドから任意のタイミングで呼ぶコールバックである。利用者は factory を生成して PeerConnectionFactory に渡すだけで、両呼び出しの並行性を制御する API はない。

## 設計方針

`formats_` はメモ化ではなく (毎回 clear + 再構築するため)、唯一の用途は `GetSupportedFormats` から `Create` へ「decoder / encoder ごとの対応フォーマット」を受け渡すことのみ。この受け渡しのために factory 全体で寿命の長い可変メンバを共有していることが競合の原因である。

### 案 1 (推奨): formats_ メンバを廃止し、Create 内でその場再計算する

`Create()` (デコーダ) / `CreateInternalVideoEncoder()` (エンコーダ) 内で、対象の decoder / encoder の対応フォーマットを `GetSupportedFormats` と同じ分岐ロジックでその場算出し、`formats_` メンバを削除する。

- 共有可変状態が消えるため、競合が根絶される。mutex も不要。
- `GetSupportedFormats` と `Create` で重複している「1 つの `VideoDecoderConfig` / `VideoEncoderConfig` から対応フォーマット列を作る」処理を `const` なヘルパ関数に切り出し、両者から呼ぶ。これによりデコーダ・エンコーダの双方を一貫した形で直せる。
- エンコーダ側の `if (formats_.empty()) { GetSupportedFormats(); }` という遅延初期化も不要になり、削除できる。

### 案 2: formats_ を mutex で保護する

`formats_` を mutex で保護し、`GetSupportedFormats` と `Create` の双方でロックする。

- 共有状態は残る。`Create` 側はロック下で `formats_` のコピーを取ってからロックを解放する必要があり、ロック範囲の管理が必要。
- 競合の根は残したまま蓋をする形になるため、案 1 を優先する。

## 完了条件

- デコーダ・エンコーダの双方で `formats_` 由来の data race / abort が解消されていること。
- 単一の `SoraVideoDecoderFactory` / `SoraVideoEncoderFactory` に対し、別スレッドから `GetSupportedFormats()` と `Create()` を並行に叩く再現テストが、修正前は abort (または ThreadSanitizer で data race 検出)、修正後は安定して完了すること。
- `CHANGES.md` の `## develop` に `[FIX]` として記載していること。

## 解決方法

未着手。

実装時の指針:

- 修正対象は `src/sora_video_decoder_factory.cpp` / `include/sora/sora_video_decoder_factory.h` と `src/sora_video_encoder_factory.cpp` / `include/sora/sora_video_encoder_factory.h`。案 1 に沿って `formats_` メンバを削除し、対応フォーマット算出を共通ヘルパへ切り出す。
- 再現テストは Catch2 ベースで `test/` 配下に追加する (既存の `test/CMakeLists.txt` で `find_package(Catch2)` 済み。`e2e` が `Catch2::Catch2WithMain` を使う前例がある)。新規ターゲットは `TEST_*` フラグでガードする。Sora サーバや E2E 基盤は不要で、factory を 1 つ生成し、複数スレッドで `GetSupportedFormats()` と `Create()` をループで叩くだけでよい。
- 検出には ThreadSanitizer もしくはハードニング有効の libc++ を用いると、修正前に確実に再現できる。
