# Intel VPL E2E の AV1 デコーダー非対応による起動失敗とクラッシュを修正する

- Created: 2026-10-05
- Completed: 2026-10-17
- Branch: feature/fix-intel-vpl-av1-e2e-startup-failure
- Polished: 2026-10-05

## 目的

Intel VPL の E2E テストが、AV1 デコーダーを非対応と判定するランナーで受信側の起動に失敗し、SIGSEGV で停止する問題を修正する。
初期化失敗を安全に扱い、Rust SDK の実装を参考に能力検出と実デコード初期化を分離する。
E2E はコーデックごとのエンコード・デコード能力に応じて構成し、対応する VPL の方向を実際の送受信で検証できるようにする。
ランナーごとの対応能力の違いはテスト対象の選択で扱い、Rust SDK と同様に利用不可のケースは理由付きで skip する。

## 現状

### 失敗したジョブ

- [失敗ジョブ](https://github.com/shiguredo/sora-cpp-sdk/actions/runs/36958222717/job/110689992118): 2026-10-02 の `E2E Test for intel_vpl`
- 対象コミット: `d0136d24ff493f39d98c22bf453bfc4d688eb68c`、ブランチ: `develop`
- 使用する sumomo のビルド対象: Ubuntu 24.04 x86_64
- libwebrtc: `m155.8059.1.1`、ビルド時の Intel VPL: `v2.17.0`
- 実行時に選ばれた VPL 実装が報告する API バージョン: `2.16`、実装種別: `HARDWARE`
- `test_sumomo_intel_vpl.py` の `test_sendonly_recvonly[VP9]` は成功し、続く `test_sendonly_recvonly[AV1]` が失敗した。
- CI は pytest の `-x` を指定しているため、残り 10 ケースは実行されていない。

送信側と受信側の両方の能力検出で、Intel VPL の AV1 は `encoder=true`、`decoder=false` となっている。
送信側は AV1 エンコーダーだけを指定するため起動できるが、受信側は AV1 デコーダーを指定するため設定の検証で失敗する。
受信側のログと pytest の例外は、次の順で出力されている。

```text
Failed to ValidateVideoCodecPreference:
decoder not supported
Failed to create VideoCodecFactory
RuntimeError: Process exited unexpectedly with code -11
```

`-11` は Linux の subprocess が SIGSEGV による終了を報告した値である。
失敗箇所は受信側の `Sumomo.__enter__()` から呼ばれる `_wait_for_startup()` であり、統計情報のアサーションには到達していない。

### ソースコードで確認した失敗経路

1. `GetVplVideoCodecCapability()` は `VplVideoDecoder::IsSupported()` の結果を AV1 の `decoder` 能力に反映する。
2. `VplVideoDecoder::IsSupported()` は `VplVideoDecoderImpl::CreateDecoder()` を呼び、4096 x 4096 と 2048 x 2048 の候補でデコーダー生成を試す。`CreateDecoderInternal()` は `Query()`、`QueryIOSurf()`、`Init()` を実行する。能力検出でも `Init()` は省略されない。
3. AV1 の `decoder=false` に対して `--av1-decoder intel_vpl` を指定すると、`ValidateVideoCodecPreference()` が失敗し、`CreateVideoCodecFactory()` が `std::nullopt` を返す。
4. `SoraClientContext::Create()` はファクトリ生成の失敗をログに出し、`nullptr` を返す。
5. `sumomo.cpp` の `main()` は戻り値を確認せず `Sumomo` を生成して `Run()` を呼ぶ。`recvonly` の `Sumomo::Run()` は `config.pc_factory = context_->peer_connection_factory()` で null のコンテキストを参照する。

この null 参照は、観測した初期化失敗後の SIGSEGV を説明するコード上の不備である。
CI ログにネイティブのバックトレースはないため、実際に停止した命令位置は未確認である。

### 直前の成功ジョブとの比較

[直前の成功ジョブ](https://github.com/shiguredo/sora-cpp-sdk/actions/runs/36951671967/job/110677671385) は、同日の 02:25 UTC に開始され、AV1 を含む全 12 ケースが成功している。
成功ジョブの対象コミットは `cb8f1e3c92e1d70aecfbb0148211f5a80feb1862` であり、失敗コミットとの差分は issue の追加と SEQUENCE の更新だけである。
SDK、sumomo、テスト、CI 設定、依存バージョンに変更はない。

GitHub Jobs API で比較すると、成功ジョブは `gmktec-k9` (runner ID: `250`)、失敗ジョブは `k9s-2` (runner ID: `8538`) で実行されている。
両方とも `self-hosted`、`linux`、`x64`、`Intel-VPL` のラベルで選択されている。
したがって、今回のコミットによるコード変更で AV1 が壊れたという根拠はなく、実行環境の比較が必要である。

### 直近 10 実行の runner name と結果

2026-10-05 に `develop` の `ci` ワークフローの直近 10 実行について、GitHub Jobs API で `E2E Test for intel_vpl` の結果と runner name を確認した。
日時は各 E2E ジョブの開始時刻であり、JST で記載する。

| ジョブ開始日時 (JST) | 結果 | runner name | Run |
| --- | --- | --- | --- |
| 2026-10-02 12:18 | failure | `k9s-2` | [36958222717](https://github.com/shiguredo/sora-cpp-sdk/actions/runs/36958222717/job/110689992118) |
| 2026-10-02 11:25 | success | `gmktec-k9` | [36951671967](https://github.com/shiguredo/sora-cpp-sdk/actions/runs/36951671967/job/110677671385) |
| 2026-10-01 11:01 | success | `gmktec-k9` | [36802470776](https://github.com/shiguredo/sora-cpp-sdk/actions/runs/36802470776/job/110183777280) |
| 2026-09-30 10:55 | success | `gmktec-k9` | [36655940834](https://github.com/shiguredo/sora-cpp-sdk/actions/runs/36655940834/job/109704487787) |
| 2026-09-29 17:00 | success | `gmktec-k9` | [36538079912](https://github.com/shiguredo/sora-cpp-sdk/actions/runs/36538079912/job/109312375791) |
| 2026-09-29 16:56 | success | `gmktec-k9` | [36537884860](https://github.com/shiguredo/sora-cpp-sdk/actions/runs/36537884860/job/109311607838) |
| 2026-09-29 16:16 | success | `gmktec-k9` | [36534275168](https://github.com/shiguredo/sora-cpp-sdk/actions/runs/36534275168/job/109299031748) |
| 2026-09-29 15:13 | success | `gmktec-k9` | [36528496985](https://github.com/shiguredo/sora-cpp-sdk/actions/runs/36528496985/job/109281183073) |
| 2026-09-29 13:45 | success | `gmktec-k9` | [36521792311](https://github.com/shiguredo/sora-cpp-sdk/actions/runs/36521792311/job/109260003252) |
| 2026-09-29 10:51 | success | `gmktec-k9` | [36508529147](https://github.com/shiguredo/sora-cpp-sdk/actions/runs/36508529147/job/109219406683) |

この範囲では、成功 9 件はすべて `gmktec-k9`、失敗 1 件は `k9s-2` であり、`k9s-2` の成功例はない。
runner name と結果の対応は確認できたが、物理マシンや GPU、ドライバー構成の違いは、この情報だけでは断定できない。

### 失敗原因の特定 (2026-10-17 実機調査)

両ランナーに入り、C++ SDK と同じ手順を再現する調査用プログラムで確認した。
GPU は両機とも Intel Arc Graphics (Meteor Lake-P)、DeviceID は `7d55/0` で同一である。
一致しないのは VPL のソフトウェアスタックだけである。

| 項目 | gmktec-k9 (成功) | k9s-2 (失敗) |
| --- | --- | --- |
| OS | Ubuntu 24.04.4 LTS | Ubuntu 26.04.1 LTS |
| libvpl2 | 1:2.13.0.0 | 1:2.16.0-1 |
| libmfx-gen | 24.3.4 | 26.1.2 |
| VA-API | 1.22.0 | 1.23.0 |

`mfxImplDescription` のコーデック一覧は両機で一致し、AV1 デコーダーは両機に存在する。
しかし `CreateDecoderInternal()` と同じ条件 (`CodecId=AV1`、`CodecProfile` 未設定、
`CodecLevel=MFX_LEVEL_AV1_2`) の `Query()` は、gmktec-k9 が `sts=0`、k9s-2 が `sts=-3` (`MFX_ERR_UNSUPPORTED`) になる。

| 条件 | gmktec-k9 (2.13) | k9s-2 (2.16) |
| --- | --- | --- |
| `CodecProfile` なし、`CodecLevel=20` | `sts=0` | `sts=-3` |
| `CodecProfile=MFX_PROFILE_AV1_MAIN`、`CodecLevel=20` | `sts=0` | `sts=0` |

したがって原因は、**AV1 の照会に `CodecProfile` を設定していないこと**である。
libvpl 2.16 はプロファイル未指定の AV1 デコード照会を拒否する。
gmktec-k9 の 2.13 はプロファイル未指定でも受理するため、同じコードでも結果が分かれていた。

`CodecProfile` と `CodecLevel` の両方を設定すると、`Query()`、`QueryIOSurf()`、`Init()` は
両機とも 4096 x 4096 と 2048 x 2048 の両方で成功することを実機で確認した。

### 非対応の実装を指定したときのクラッシュ

`SoraClientContext::Create()` が `nullptr` を返した場合、`sumomo.cpp` の `main()` は
戻り値を確認せずに `Sumomo` を構築する。`Sumomo::Run()` は
`config.pc_factory = context_->peer_connection_factory()` で null のコンテキストを参照する。

実機で確認すると、非対応の実装を指定した場合は必ず SIGSEGV で終了する (終了コード `139`)。

| 指定 | 終了コード |
| --- | --- |
| `--vp8-decoder intel_vpl` | `139` |
| `--vp8-encoder intel_vpl` | `139` |
| `--av1-decoder nvidia_video_codec` | `139` |
| `--h264-encoder amd_amf` | `139` |

これは AV1 の `CodecProfile` とは独立した不備であり、AV1 を修正しても他のコーデックで同じクラッシュが起きる。

### Rust SDK における対応

2026-10-05 に [sora-rust-sdk の調査対象コミット](https://github.com/shiguredo/sora-rust-sdk/commit/935280d23614da266884d57b0cbdc2e2f9878d0e) と、Cargo.lock が固定する `shiguredo_vpl 2026.4.0` のソースを確認した。
公開 crate のソースが示す VCS コミットは [vpl-rs の調査対象コミット](https://github.com/shiguredo/vpl-rs/commit/1e0bee250f62eb8fe1b43163c4253b435cdfdd6f) である。

#### 能力検出と実デコード初期化

- Rust SDK の `VplVideoCodecCapability::new()` は `list_adapters()` で選んだアダプターの DRM render node を保持し、`collect_supported_formats()` でエンコード・デコードそれぞれの対応形式を取得する。能力検出と実際のエンコーダー・デコーダー生成には、同じアダプター指定を使用する。
- `codec_info.rs` の `supported_codecs()` は、ハードウェア実装と DRM render node で絞った `MFXEnumImplementations()` の `mfxImplDescription` を参照する。`probe_decoding()` はデコーダー一覧に対象の CodecID があるかを判定するため、能力検出で固定解像度の `Query()` や `Init()` は実行しない。
- `decode.rs` の `Decoder::new()` はセッションを作成し、デコーダーの初期化を最初の `decode()` まで遅延させる。`Decoder::initialize()` は `MFXVideoDECODE_DecodeHeader()` で受信ビットストリームから解像度などを取得してから `MFXVideoDECODE_Init()` を呼ぶ。4096 x 4096 / 2048 x 2048 の固定解像度や `MFX_LEVEL_AV1_2` の固定指定は使用しない。
- `SoraConnectionContext::new_with_config()` は `Result` を返し、sumomo の `main.rs` はコンテキスト生成失敗を `?` で伝える。生成に失敗したコンテキストで接続処理を開始しない。

#### エンコード・デコード能力別の E2E

`vpl_video_codec.rs` は `vpl_fully_supported_codecs()`、`vpl_encoder_supported_only_codecs()`、`vpl_decoder_supported_only_codecs()` で対象を分ける。
`create_vpl_context()` は `VideoCodecPreference::new_from_capability()` を使い、検出された方向とコーデックだけを VPL の優先設定に登録する。

| VPL の対応状況 | Rust SDK の E2E 構成 |
| --- | --- |
| エンコード・デコードの両方に対応 | VPL 送信 → VPL 受信、および VPL 同士の sendrecv |
| エンコードのみ対応 | VPL 送信 → 既定の内部デコーダーで受信 |
| デコードのみ対応 | 既定の内部エンコーダーで送信 → VPL 受信 |

今回観測した AV1 の `encoder=true`、`decoder=false` なら、`test_vpl_encoder_only_sendonly()` の対象となり、VPL の AV1 エンコードを検証する。
相手側の内部デコーダーが対応している場合に実行し、`run_sendonly_recvonly_with_contexts()` は対象コーデックの MIME type、送受信パケット数と `framesDecoded > 0` を確認する。
能力別の E2E 分岐は、2026-04-09 の [VPL 対応コミット](https://github.com/shiguredo/sora-rust-sdk/commit/02e3b026651a5eeb3b6f2ae0a64ed5bbe50940a2) から存在する。

Rust SDK は VPL の能力取得に失敗した場合や、対象コーデック・相手側の内部実装が非対応の場合、理由を出力してテストから return / continue する。
このため CI の成功だけでは、AV1 の VPL デコードが実行されたことを保証できない。
また、`k9s-2` の非対応判定が固定パラメーターによる誤判定なのか、実際のハードウェア・実装の非対応なのかは、このソース比較だけでは確定できない。
同じランナーで Rust SDK と C++ SDK を比較する必要がある。

### 再現手順

1. 失敗ジョブと同じ Intel VPL ランナーで、対象コミットの Ubuntu 24.04 x86_64 向け sumomo を用意する。
2. `sumomo --log-level verbose --show-video-codec-capability` を実行し、Intel VPL の AV1 Decoder が能力一覧に出ないかと、能力検出に失敗した API、解像度、ステータスを確認する。
3. `TEST_SIGNALING_URL`、`TEST_CHANNEL_ID_PREFIX`、`TEST_SECRET_KEY` を通常の E2E 実行環境に設定する。
4. リポジトリルートから CODEBASE.md の E2E 実行規約に従い、`INTEL_VPL=1` を設定して `test_sumomo_intel_vpl.py::test_sendonly_recvonly[AV1]` を `-v -s --timeout=60` 付きで単独実行する。
5. AV1 の能力が `decoder=false` の場合、受信側の設定検証が失敗した後に終了コード `-11` となることを確認する。

2026-10-17 に `k9s-2` で手順 2 と 4 を実行し、`--show-video-codec-capability` に
AV1 Decoder が現れないことと、`test_sendonly_recvonly[AV1]` が `-11` で失敗することを確認した。

### 既存 issue との関係

AV1 / H.265 の少数ストリームのサイマルキャスト問題、小さい解像度の AV1 エンコード問題、VPL デコード中の無限ループとは、発生段階が異なる。
今回はサイマルキャストと実フレームのデコードを開始する前に、能力検出とコンテキスト生成で失敗している。
既存の open / pending / closed issue、CHANGES.md、関連するコミット履歴に、同じ起動失敗を解決した記録は見つからなかった。
上記は C++ SDK 内の確認結果であり、Rust SDK には能力検出・初期化と能力別 E2E の参考実装が存在する。
ただしこれらに合わせる作業は今回の起動失敗の修正とは別であり、今回のスコープには含めない。

## 設計方針

### AV1 の照会に CodecProfile を設定する

`VplVideoDecoderImpl::CreateDecoderInternal()` の AV1 の分岐で、`CodecProfile` に
`MFX_PROFILE_AV1_MAIN` を設定する。`CodecLevel` の指定はそのまま残す。
この 2 つを両方設定しないと、libvpl 2.16 では `Query()` が `MFX_ERR_UNSUPPORTED` になる。

### 初期化失敗によるクラッシュを防ぐ

`sumomo.cpp` の `main()` で `SoraClientContext::Create()` の戻り値を確認する。
`nullptr` の場合は英語のエラーログを出力し、非ゼロの終了コードで終了する。
失敗したコンテキストで `Sumomo::Run()` を呼ばない。

### 今回のスコープに含めないもの

能力検出を `mfxImplDescription` ベースに変更すること、実デコード初期化を `DecodeHeader()` ベースに
変更すること、E2E を対応方向ごとに再構成することは、Rust SDK の実装に合わせる作業であり、
今回の起動失敗の修正とは別である。今回は行わない。

## テスト戦略

- 失敗した `k9s-2` と成功した `gmktec-k9` の実機で、AV1 の能力出力に Decoder が現れることと、
  `test_sumomo_intel_vpl.py` の既存 12 ケースが全て成功することを確認する。
- 修正前の sumomo を同じ `k9s-2` で実行し、AV1 Decoder が能力出力に現れないことと、
  `test_sendonly_recvonly[AV1]` が `-11` で失敗することを確認して、修正の前後を比較する。
- 非対応の実装を sumomo に指定し、SIGSEGV とならずエラーログと非ゼロの終了コードを返すことを確認する。
  モックやスタブは利用しない。

## 完了条件

- AV1 デコーダーが非対応と誤判定されなくなり、`k9s-2` の既存 12 ケースが全て成功する。
- sumomo がコンテキスト生成に失敗しても null 参照せず、原因を示すエラーログと非ゼロの終了コードで終了する。
- 失敗ランナーの AV1 デコーダー非対応判定について、失敗した条件が特定され、対応内容が記録されている。

## 解決方法

`VplVideoDecoderImpl::CreateDecoderInternal()` の AV1 の分岐で、`CodecProfile` に
`MFX_PROFILE_AV1_MAIN` を設定するようにした。`CodecLevel` の指定はそのまま残している。

原因は AV1 の照会に `CodecProfile` を設定していなかったことである。libvpl 2.16 は
プロファイル未指定の AV1 デコード照会を `MFX_ERR_UNSUPPORTED` で拒否するため、AV1 が
`decoder=false` と判定されていた。`CodecProfile` と `CodecLevel` の両方を設定すると、
`Query()`、`QueryIOSurf()`、`Init()` は 4096 x 4096 と 2048 x 2048 の両方で成功する。

あわせて、`sumomo.cpp` の `main()` で `SoraClientContext::Create()` の戻り値を確認し、
`nullptr` の場合はエラーログを出力して非ゼロの終了コードで終了するようにした。
これは AV1 とは独立した不備であり、非対応の実装を指定した場合に必ず SIGSEGV で終了することを
実機で確認している。

`k9s-2` (libvpl 2.16) で既存の `test_sumomo_intel_vpl.py` の 12 ケースを変更せずに実行し、
全て成功することを確認した。修正前の sumomo では `test_sendonly_recvonly[AV1]` が
`decoder not supported` で終了コード `-11` となることも確認している。

VPL の能力検出を `mfxImplDescription` ベースに変更すること、実デコード初期化を
`DecodeHeader()` ベースに変更すること、E2E を対応方向ごとに再構成することは、
Rust SDK の実装に合わせる別の作業であるため、今回は行っていない。
