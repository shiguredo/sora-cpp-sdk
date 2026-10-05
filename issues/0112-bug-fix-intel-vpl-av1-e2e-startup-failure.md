# Intel VPL E2E の AV1 デコーダー非対応による起動失敗とクラッシュを修正する

- Created: 2026-10-05
- Completed: {YYYY-MM-DD}
- Branch: feature/fix-intel-vpl-av1-e2e-startup-failure
- Polished: {YYYY-MM-DD}

## 目的

Intel VPL の E2E テストが、AV1 デコーダーを非対応と判定するランナーで受信側の起動に失敗し、SIGSEGV で停止する問題を修正する。
初期化失敗を安全に扱い、Rust SDK の実装を参考に能力検出と実デコード初期化を分離する。
E2E はコーデックごとのエンコード・デコード能力に応じて構成し、対応する VPL の方向を実際の送受信で検証できるようにする。

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

### 未特定の点

AV1 デコーダーが非対応と判定された理由は未特定である。
GPU の能力、ドライバーや VPL 実装の差、能力検出時のパラメーターやリソース状態のいずれに起因するかは、現在のログからは判断できない。
`Query()` と `Init()` の失敗理由は verbose ログで出力されるが、今回のログにはその詳細がない。
また、ランナー一覧 API は権限不足で取得できず、各ランナーの GPU とドライバー構成は確認できていない。
ビルド時の VPL バージョンと実行時 API バージョンが異なることだけで、原因をバージョン不一致と断定しない。

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

### 既存 issue との関係

AV1 / H.265 の少数ストリームのサイマルキャスト問題、小さい解像度の AV1 エンコード問題、VPL デコード中の無限ループとは、発生段階が異なる。
今回はサイマルキャストと実フレームのデコードを開始する前に、能力検出とコンテキスト生成で失敗している。
既存の open / pending / closed issue、CHANGES.md、関連するコミット履歴に、同じ起動失敗を解決した記録は見つからなかった。
上記は C++ SDK 内の確認結果であり、Rust SDK には能力検出・初期化と能力別 E2E の参考実装が存在する。

## 設計方針

### 初期化失敗によるクラッシュを防ぐ

`sumomo.cpp` の `main()` で `SoraClientContext::Create()` の戻り値を確認する。
`nullptr` の場合は英語のエラーログを出力し、非ゼロの終了コードで終了する。
失敗したコンテキストで `Sumomo::Run()` を呼ばない。
SDK と sumomo は、明示的に指定された非対応の実装を設定エラーとして扱い、その原因をログで追えるようにする。

### 能力検出と実デコード初期化を分離する

成功した `gmktec-k9` と失敗した `k9s-2` で同一ビルドの能力検出を比較し、GPU、ドライバー、VPL 実装と verbose ログの差を調べる。
特に `CreateDecoderInternal()` の `Query()`、`QueryIOSurf()`、`Init()` のどこで失敗するかを確認する。
4096 x 4096 と 2048 x 2048 のプローブ、AV1 の `MFX_LEVEL_AV1_2` 指定、選択された実装と実際の対応解像度を照合する。
実機情報を成果物に残す際は、内部エンドポイントや認証情報を除く。

同じランナーで Rust SDK の `supported_codecs()` が参照する対応一覧と C++ SDK のプローブ結果を比較し、AV1 の非対応判定の理由を確認する。
能力検出は Rust SDK と同様に、選択したハードウェア実装の `mfxImplDescription` からエンコード・デコードそれぞれの対応コーデックを取得する方針とする。
能力検出と実際のコーデック生成で、対象の VPL 実装・アダプターが一致するようにする。

実デコード初期化は `VplVideoDecoderImpl::InitVpl()` の固定解像度・固定 AV1 レベル指定を見直し、最初の受信ビットストリームを `DecodeHeader()` で解析してから `Init()` を行う方針とする。
`Configure()`、`Decode()`、`Release()` の責務とリソース管理をこの初期化順序に合わせ、ヘッダー不足や初期化失敗をエラーとして安全に扱う。
能力一覧に対応として載る場合も、実際の初期化・送受信まで確認する。

### E2E を対応する方向ごとに構成する

`test_sumomo_intel_vpl.py` は sumomo の能力表示を利用して、VP9 / AV1 / H.264 / H.265 のエンコード・デコード対応を取得する。
テスト開始前に対象コーデックと必要な方向を確認し、次の構成を選択する。

- 両方向に対応するコーデックは、既存の VPL 同士の sendonly / recvonly と sendrecv で検証する。
- エンコードのみ対応するコーデックは、VPL の sendonly と内部デコーダーの recvonly で検証する。
- デコードのみ対応するコーデックは、内部エンコーダーの sendonly と VPL の recvonly で検証する。
- サイマルキャストは VPL エンコード対応を条件とし、VPL デコードの非対応だけを理由に実行対象から外さない。
- 非対応の方向を要求するケースと、相手側の内部実装も非対応のケースは、コーデック・方向・理由を示して skip する。

内部実装を使うケースは独立したテストとして明示し、VPL 側の `encoderImplementation` または `decoderImplementation` が `libvpl` であることを検証する。
VPL 同士のケースは両方向の実装名を維持して確認する。
受信するケースでは `packetsReceived` に加えて `framesDecoded > 0` を確認し、フレームのデコードまで到達したことを検証する。
能力検出が対応を報告した方向で起動・初期化・送受信に失敗した場合は、テストを失敗させる。

CI では能力取得の実行失敗・結果の解釈失敗と、正常に取得した結果でのコーデック非対応を区別する。
能力取得に失敗した場合、VPL 実装が見つからない場合、または実行できる VPL のケースが 1 件もない場合は、理由を示してジョブを失敗させる。
能力に応じたテスト選択を成立させるために、全コーデックの VPL 送受信対応をランナーの必須条件にはしない。

## テスト戦略

- 実際に非対応と判定されるコーデック実装を sumomo に指定し、コンテキスト生成失敗時に SIGSEGV とならず、エラーログと非ゼロの終了コードを返すことを確認する。モックやスタブは利用しない。
- 成功ランナーと失敗ランナーで、同一ビルドの能力検出結果と AV1 の単独 E2E 結果を比較する。同じ環境の Rust SDK の対応一覧とも照合し、能力検出・初期化変更の前後を記録する。
- VPL の両方向に対応する環境では AV1 を含む VPL 同士の送受信を検証し、両方向の実装名が `libvpl` であることと `framesDecoded > 0` を確認する。
- AV1 のエンコードのみ対応する環境では、VPL エンコーダー → 内部デコーダーのケースを実行し、VPL エンコード、送受信と実デコードを確認する。非対応の VPL デコーダーを要求するケースは理由付きで skip され、サイマルキャストは引き続き実行されることを確認する。
- デコードのみ対応するコーデックがある実環境では、内部エンコーダー → VPL デコーダーのケースを確認する。該当する実環境がない場合は未検証と記録し、モックや能力結果の偽装で検証済みとしない。
- 能力取得の実行・解釈に失敗した場合や、VPL を検証するケースが 1 件も実行できない場合に、CI が全 skip の成功扱いにならないことを確認する。
- Intel VPL の既存 12 ケースと能力別に追加したケースを、CODEBASE.md の規約に従って各ケース単独で実行する。対応能力ごとの実行・skip と理由を記録し、実行したケースの回帰がないことを確認する。

調査段階では CI ログ、Jobs API、C++ SDK と Rust SDK のソースコード、Rust SDK が使用する VPL crate、直前の成功ジョブを確認した。
Intel VPL の実機での再実行とネイティブのバックトレース取得は未実施である。

## 完了条件

- sumomo がコンテキスト生成に失敗しても null 参照せず、原因を示すエラーログと非ゼロの終了コードで終了する。
- 失敗ランナーの AV1 デコーダー非対応判定について、失敗した API と条件、または実機の非対応能力が特定され、対応内容が記録されている。
- VPL の能力検出が固定解像度のデコーダー生成に依存せず、実デコード初期化は受信ビットストリームに基づくパラメーターを使用する。
- E2E は検出した対応能力に応じて両方向・エンコードのみ・デコードのみのケースを選択し、実行対象と skip 理由を記録する。
- AV1 エンコードのみ対応する環境でも VPL の AV1 エンコードを実際の送受信で検証でき、デコード対応環境では VPL の AV1 デコードも検証できる。
- 能力検出が対応を報告した方向では実際の送受信が成功し、受信側は `framesDecoded > 0` を満たす。既存の対応するケースとサイマルキャストの検証を維持する。
- 能力取得失敗や VPL を検証するケースが全件実行不能の場合は、CI が原因を示して失敗する。
