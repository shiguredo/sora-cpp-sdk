# Intel VPL E2E の AV1 デコーダー非対応による起動失敗とクラッシュを修正する

- Created: 2026-10-05
- Completed: {YYYY-MM-DD}
- Branch: feature/fix-intel-vpl-av1-e2e-startup-failure
- Polished: {YYYY-MM-DD}

## 目的

Intel VPL の E2E テストが、AV1 デコーダーを非対応と判定するランナーで受信側の起動に失敗し、SIGSEGV で停止する問題を修正する。
初期化失敗を安全に扱うとともに、E2E が要求するコーデック能力を満たす環境でテストを実行できるようにする。

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

## 設計方針

### 初期化失敗によるクラッシュを防ぐ

`sumomo.cpp` の `main()` で `SoraClientContext::Create()` の戻り値を確認する。
`nullptr` の場合は英語のエラーログを出力し、非ゼロの終了コードで終了する。
失敗したコンテキストで `Sumomo::Run()` を呼ばない。
これにより初期化失敗の原因をログで追えるようになるが、AV1 の送受信能力が不足した環境で E2E が成功するわけではない。

### AV1 の非対応判定を解消する

成功した `gmktec-k9` と失敗した `k9s-2` で同一ビルドの能力検出を比較し、GPU、ドライバー、VPL 実装と verbose ログの差を調べる。
特に `CreateDecoderInternal()` の `Query()`、`QueryIOSurf()`、`Init()` のどこで失敗するかを確認する。
4096 x 4096 と 2048 x 2048 のプローブ、AV1 の `MFX_LEVEL_AV1_2` 指定、選択された実装と実際の対応解像度を照合する。
実機情報を成果物に残す際は、内部エンドポイントや認証情報を除く。

実際に AV1 デコード非対応の環境であれば、VP9 / AV1 / H.264 / H.265 の送受信を確認したランナーを専用ラベルで選択するか、ランナー環境をその要件に合わせる。
AV1 デコード対応環境でプローブだけが失敗している場合は、失敗した API と条件を根拠として能力検出を修正する。
能力検出の候補を変更する場合は、同じ候補を使う `VplVideoDecoderImpl::InitVpl()` の実デコード初期化とも整合させる。
能力一覧を確認しただけで対応済みとせず、実際の送受信で確認する。

CI の Intel VPL ジョブでは、E2E の開始前に必要な各コーデックのエンコーダーとデコーダーの能力を確認し、不足時は対象コーデックと実装が分かるエラーで失敗させる。
AV1 ケースの無条件 skip、内部デコーダーへのフォールバック、リトライだけで成功扱いにする対策は採らない。
これらでは `libvpl` による送受信という既存テストの目的を満たせないためである。

## テスト戦略

- 実際に非対応と判定されるコーデック実装を sumomo に指定し、コンテキスト生成失敗時に SIGSEGV とならず、エラーログと非ゼロの終了コードを返すことを確認する。モックやスタブは利用しない。
- 成功ランナーと失敗ランナーで、同一ビルドの能力検出結果と AV1 の単独 E2E 結果を比較する。ドライバー更新やプローブ変更を行う場合は変更前後を記録する。
- Intel VPL の `test_sendonly_recvonly[AV1]` と `test_sendrecv[AV1]` で `encoderImplementation` と `decoderImplementation` が `libvpl` であることを確認する。
- Intel VPL の既存 12 ケースを CODEBASE.md の規約に従って各ケース単独で実行し、回帰がないことを確認する。AV1 の非対応判定が間欠的な場合は、能力検出と AV1 の単独ケースを反復して確認する。

調査段階では CI ログ、Jobs API、ソースコード、直前の成功ジョブを確認した。
Intel VPL の実機での再実行とネイティブのバックトレース取得は未実施である。

## 完了条件

- sumomo がコンテキスト生成に失敗しても null 参照せず、原因を示すエラーログと非ゼロの終了コードで終了する。
- 失敗ランナーの AV1 デコーダー非対応判定について、失敗した API と条件、または実機の非対応能力が特定され、対応内容が記録されている。
- Intel VPL E2E の実行環境が必要な各コーデックの送受信能力を満たし、不足時には E2E 開始前に対象を示して失敗する。
- AV1 の送受信を `libvpl` で検証でき、既存 12 ケースが成功する。
