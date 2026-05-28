# SoraVideoDecoderFactory / SoraVideoEncoderFactory の formats_ 並行アクセスによる abort を修正する

- Priority: High
- Created: 2026-05-27
- Model: Opus 4.7
- Branch: feature/fix-video-factory-formats-data-race-abort
- Polished: 2026-05-28

## 目的

`SoraVideoDecoderFactory::Create()` がプロセスごと abort (SIGABRT) するクラッシュを解消する。

観測された事実は「デコーダ生成時に `SoraVideoDecoderFactory::Create()` の内部で abort する」ことで、原因はコードレビューで特定した「`formats_` メンバの未同期な並行アクセス」だと考えられる。`SoraVideoDecoderFactory` / `SoraVideoEncoderFactory` は libwebrtc の factory 実装で、メソッドは libwebrtc が内部スレッドから呼ぶため、SDK 利用側にこの並行性を制御する手段がない。マルチコーデックかつ参加者が出入りする多人数セッションで踏みうるため、安定性への影響が大きい。観測されたのはデコーダだが、`SoraVideoEncoderFactory` も同種の潜在欠陥を持つため、両 factory を一体で修正する。詳細な機序は後述する。

## 優先度根拠

High とする。

- abort はプロセスごと落ちる (SIGABRT) ため、SDK を組み込んだアプリケーションを巻き込んで停止する。C++ 例外として捕捉できない。`GetSupportedFormats` / `Create` は libwebrtc が内部スレッドから呼ぶコールバックで、利用者は factory を生成して渡すだけであり、両呼び出しの並行性を制御する API が無いため、利用側で回避できない。
- 「受信中は再ネゴシエーションしない」以外に避ける道がなく、それは多人数セッションでは非現実的な制約。通常利用の範囲内で発生する。
- 未同期の共有可変状態という欠陥はコード上に明確に存在し、偶発的なものではない。

## 現状

### 観測されたクラッシュ

実利用環境で、デコーダ生成経路 (`SoraVideoDecoderFactory::Create`) の内部で abort するクラッシュが観測されている。特定のコーデックに依存せず発生しており、これは後述の `formats_` への添字アクセスが全コーデック共通の経路であることと符合する。

### 根本原因 (デコーダ)

`SoraVideoDecoderFactory` の `formats_` に対する未同期な並行アクセス。

- `include/sora/sora_video_decoder_factory.h:70` の `mutable std::vector<std::vector<webrtc::SdpVideoFormat>> formats_;` は保護する mutex を持たない。
- `GetSupportedFormats() const` (`src/sora_video_decoder_factory.cpp:50`) は呼ばれるたびに `formats_.clear()` (51 行目) してから decoder ごとに `formats_.push_back(...)` (75 行目) で作り直す。clear 中〜再構築途中は `formats_` のサイズが 0〜decoder 数未満の不整合状態になり、`push_back` の再確保で要素のアドレスも移動する。
- `Create()` (`src/sora_video_decoder_factory.cpp:81`) は `formats_[n++]` (95 行目) で添字アクセスし、`formats_.size() == config_.decoders.size()` を暗黙の前提にしている。
- この 2 メソッドは libwebrtc が別々の内部スレッドから呼ぶ (典型的には `GetSupportedFormats` はネゴシエーション時、`Create` はデコーダ生成時と考えられる)。`Create` が clear / 再構築途中の `formats_` を読むと `n >= formats_.size()` で範囲外アクセスになる。本プロジェクトは全ビルドターゲットで libc++ を `_LIBCPP_HARDENING_MODE_EXTENSIVE` (CMakeLists.txt / run.py) でビルドしており、`std::vector::operator[]` に境界チェックが入るため範囲外アクセスは `abort()` に至る。境界チェックが無くても、再確保で移動・破棄された記憶域を読む未定義動作である。
- なお `Create` には、エンコーダ側 (後述) の `if (formats_.empty()) GetSupportedFormats();` のような遅延初期化ガードが無い。`GetSupportedFormats` の `clear()` 直後 (サイズ 0) に `Create` が走れば、その時点で `formats_[0]` は確実に範囲外になる。

### エンコーダ側の同種パターンと相違

`SoraVideoEncoderFactory` も「未同期の共有可変 `formats_` を添字前提で読む」という同種の静的パターンを持つ。

- `include/sora/sora_video_encoder_factory.h:104` に保護なしの `formats_`。
- `GetSupportedFormats() const` (`src/sora_video_encoder_factory.cpp:61`) が `formats_.clear()` (62 行目) → `push_back` (86 行目) で再構築する。
- `CreateInternalVideoEncoder()` (`src/sora_video_encoder_factory.cpp:92`) が `formats_[n++]` (111 行目) で添字アクセスし、`if (formats_.empty()) GetSupportedFormats();` (96-98 行目) で遅延初期化する。

ただし並行が成立する経路はデコーダと異なる。

- 外側 factory の `Create()` (`src/sora_video_encoder_factory.cpp:138`) は `internal_encoder_factory_` がある場合 `SimulcastEncoderAdapter` を返すだけで、外側の `formats_` を読まない。危険な添字読みは内側 factory の `CreateInternalVideoEncoder` でのみ起きる。
- 内側 factory の `formats_` は遅延初期化 (empty のときだけ構築) で、`SimulcastEncoderAdapter` は内側 factory に対し `Create` のみを呼び `GetSupportedFormats` を呼ばないため (確認した範囲)、デコーダのような「再ネゴシエーションのたびに clear」競合は持たない。
- したがって競合窓はデコーダより狭いが、複数スレッドが初回の `CreateInternalVideoEncoder` を同時に実行すると、遅延初期化 (clear + 構築) と `formats_[n++]` が交錯して同種の破綻が起こりうる。

デコーダで abort が観測されエンコーダは潜在という状況は、この非対称性 (デコーダは毎回 clear し、遅延初期化ガードも無い) と整合する。

## 設計方針

`formats_` の用途は、デコーダでは `GetSupportedFormats` から `Create` への「decoder ごとの対応フォーマット」の受け渡し、エンコーダでは加えて内側 factory の遅延キャッシュである。これを安全にする方向は次の 3 つで、それぞれにトレードオフがある。`Create` はストリーム構成時のみで低頻度なため性能はいずれも問題にならず、安全性を最優先に選ぶ。

### 案 1: formats_ メンバを廃止し、その場で再計算する

`Create()` / `CreateInternalVideoEncoder()` 内で、対象 config の対応フォーマットを `GetSupportedFormats` と同じ分岐 (factory 経由 / `get_supported_formats` / `GetDefaultVideoFormats`) でその場算出し、`formats_` メンバを削除する。

- 利点: 共有可変状態が消えるため `formats_` 競合が根絶される。mutex 不要でデッドロックの懸念も無い。添字と `config_.decoders` / `config_.encoders` の順序整合の暗黙前提も消え、エンコーダの遅延初期化も不要になる。
- 共通化できるのは「1 つの config から対応フォーマット列を作る」処理のみ。`VideoDecoderConfig` と `VideoEncoderConfig` は別の型だが、算出に使う `codec` / `get_supported_formats` は同名同型、`factory` は同名で型のみ異なり (`VideoDecoderFactory` / `VideoEncoderFactory`)、呼び出す `GetSupportedFormats()` は両者に存在するため、`template<class Config>` のフリー関数 1 つ (内部ヘッダか各 `.cpp` の無名名前空間) にまとめられる。ループ本体は変数名以外が同一になる。エンコーダ固有の `alignment` 決定 (`src/sora_video_encoder_factory.cpp:119-127`)、create クロージャ構築、`SimulcastEncoderAdapter` 等のラップ処理 (`src/sora_video_encoder_factory.cpp:138-176`) は各 `Create` 側に残し、変更しない。
- リスク (factory 並行呼び出し): factory 分岐 (標準構成では `kInternal` コーデックが該当。`sora_video_codec_factory.cpp:135,214` で `builtin_decoder_factory` / `builtin_encoder_factory` を使う) では、`Create` (worker スレッド) が `factory->GetSupportedFormats()` を呼ぶことになる。現状この呼び出しは `GetSupportedFormats()` (signaling スレッド) のみだが、案 1 後は両者が同一 factory の `GetSupportedFormats()` を並行に呼びうる。`formats_` の確実な data race は消えるが、代わりに内部 factory の `GetSupportedFormats()` の並行呼び出しが安全かは factory 実装依存になる。`webrtc::CreateBuiltinVideoDecoderFactory()` 由来はステートレスで安全な見込みだが、Mac (`CreateMacVideoDecoderFactory`) / Android (`CreateAndroidVideoDecoderFactory`) の内蔵 factory のスレッド安全性は未確認。案 1 を採るならこの確認が前提。
- `formats_` は private メンバのため公開 API のシグネチャは変わらない。SDK は静的ライブラリ配布で利用側も再ビルドする前提のため、メンバ削除による ABI 変化は許容範囲。

### 案 2: formats_ を mutex で保護する

`formats_` を mutex で保護し、`GetSupportedFormats` と `Create` の双方でロックする。

- 利点: `factory->GetSupportedFormats()` は `GetSupportedFormats()` でのみ呼ばれるため、案 1 の factory 並行呼び出しは発生しない。共有状態は残るが factory 実装への依存は無い。
- リスク 1 (自己デッドロック): エンコーダの `CreateInternalVideoEncoder` は `if (formats_.empty()) GetSupportedFormats();` (`sora_video_encoder_factory.cpp:96-98`) で遅延初期化する。ロック保持中に `GetSupportedFormats()` (同じ mutex を取る) を呼ぶと非再帰 mutex で自己デッドロックする。ロックを取らない内部の再構築ヘルパを用意して両者から呼ぶか、遅延初期化を廃止する設計が必須。デコーダには遅延初期化が無いのでこの罠は出ない。
- リスク 2 (ロック保持中の外部コールバック): `GetSupportedFormats()` のループは `dec.factory->GetSupportedFormats()` / `dec.get_supported_formats()` (ユーザー提供コールバック) を呼ぶ。これらをロック区間内で実行すると、外部コードがロック下で走り、別ロックとの順序逆転や再入の温床になる。
- リスク 3 (ロック範囲): `Create` はロック中に `formats_[n]` のコピーを取り (現状コードは既にコピーしている)、`create_video_decoder(format)` (デコーダ生成 = 外部コード) はロック外で呼ぶ必要がある。
- 上記 3 点を正しく設計すれば安全。

### 案 3: formats_ を初回のみ構築して read-only 化する

`GetSupportedFormats` が毎回 `clear()` → 再構築する必然性が無く、対応フォーマットがセッション中不変であれば、`std::call_once` 等で初回だけ `formats_` を構築し以降は読み取り専用にする。read-only な vector の同時読み取りは安全なので、mutex も factory 並行呼び出しも不要になり、案 1・案 2 双方のリスクを回避できる。

- 前提: 「`GetSupportedFormats` が毎回再構築している現状の意図」の確認が要る。factory の対応フォーマットが実行中に変化しうるなら不変化できない。変化しないなら最も素直で安全。

### 選択方針

安全性を最優先とし、(1) `formats_` を不変化できる (対応フォーマットが不変) なら案 3、(2) 不変化できないなら案 2 を上記リスク 1-3 を回避する形で採用、(3) 案 1 は Mac / Android 内蔵 factory の `GetSupportedFormats()` のスレッド安全性を確認できた場合に限る、という優先順位とする。

## 完了条件

- デコーダ・エンコーダの双方で `formats_` 由来の data race / abort が解消されていること。
- デコーダの再現テストが、修正後に abort せず安定して完走すること。エンコーダは内側 factory の経路が外側経由のテストでは再現しないため、採用案による解消をコードレビューで確認し、テストでの再現は対象外とする (理由は「現状」を参照)。
- 既存の `e2e` テストが従来通り、デコーダ / エンコーダ生成経路に回帰が無いこと。
- `CHANGES.md` の `## develop` に、`- [FIX] ...を修正する` 行とその下にインデントした著者行 (`  - @xxx`) を、デコーダ・エンコーダ双方が対象と読み取れる形で追記していること。

## 解決方法

未着手。

実装時の指針:

- 修正対象は `src/sora_video_decoder_factory.cpp` / `include/sora/sora_video_decoder_factory.h` と `src/sora_video_encoder_factory.cpp` / `include/sora/sora_video_encoder_factory.h`。設計方針で採用した案に従い `formats_` 由来の競合を解消する (案ごとの実装上の注意は設計方針を参照)。
- 再現テストは Catch2 ベースで `test/` 配下に、`e2e` とは独立した実行ファイルとして追加する (`e2e` は `TEST_SIGNALING_URL` 等を要求し実 Sora サーバへ接続するため混ぜない)。本テストは Sora サーバ・環境変数に依存しない。`test/CMakeLists.txt` は `find_package(Catch2)` 済みで、新ターゲットを `TEST_*` フラグでガードし `Catch2::Catch2WithMain` をリンクする。
- テスト構成: `GetSoftwareOnlyVideoDecoderFactoryConfig()` で config を作って `SoraVideoDecoderFactory` を構築し、`webrtc::CreateEnvironment()` (`api/environment/environment_factory.h`) で `Environment` を用意する。2 スレッドを `std::barrier<>` (完了関数は渡さない。完了関数付きは libc++ の dylib シンボルに依存する) か `std::atomic<bool>` のスピンで同時開始し、一方が `GetSupportedFormats()`、他方が `Create(env, format)` を各数千回ループで叩く。
- テスト検証: `format` は `GetSupportedFormats()` の戻り値 (最も単純には VP8) を渡す。abort せず完走し、かつ `Create` が非 null を返すことを確認する (競合解消後にデコーダ生成が壊れていないことも担保する)。実行は数秒〜十数秒に収める。
- 再現に ThreadSanitizer は使わない (プロジェクトに導入されておらず、`run.py` / CMake に sanitizer オプションが無い)。再現は `_LIBCPP_HARDENING_MODE_EXTENSIVE` が効いた SDK 本体の `Create` が abort することに依存する。テストは `Sora::sora` をリンクしてその `Create` を呼ぶため、テストターゲット側に hardening フラグは不要。
- テスト実行経路: `run.py` のテストビルドは `--test` でガードされ、native ビルド (`build == target`) のみが対象。既存 `e2e` の実行は `--run-e2e-test` ガード下にあるが、本テストは Sora 非依存なので `--run-e2e-test` には乗せず、`--test` の native ビルドでビルド後そのまま実行する経路を追加する。起動は既存 `e2e` の呼び出しに倣う (Windows は実行ファイルが `configuration` サブディレクトリに出るパス分岐がある)。
