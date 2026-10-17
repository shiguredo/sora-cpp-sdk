# Intel VPL の能力検出と初期化を固定解像度への依存から外す

- Created: 2026-10-17
- Completed: {YYYY-MM-DD}
- Branch: feature/refactor-vpl-capability-detection
- Polished: {YYYY-MM-DD}

## 目的

Intel VPL の能力検出と実デコード初期化が、固定解像度での生成を試す方式になっている。
この方式は、実装や解像度の組み合わせによって対応しているコーデックでも非対応と判定する。
0112 では AV1 デコーダーがこの方式で非対応と誤判定され、E2E の受信側が起動に失敗して SIGSEGV に至った。

0112 の修正は AV1 の `CodecProfile` の指定漏れという個別の原因に対処したもので、
固定解像度の生成に依存している構造そのものは残っている。
同じ構造に起因する誤判定が別のコーデックや別の VPL 実装で再発しうるため、
能力検出と初期化を固定解像度への依存から外す。

Rust SDK (sora-rust-sdk とその依存である vpl-rs) には参考実装がある。
ただしこれを正解として移植するのではなく、C++ SDK 側で実機検証したうえで採否を決める。

## 現状

### 能力検出が固定解像度の生成に依存している

`src/hwenc_vpl/vpl_video_codec.cpp` の `GetVplVideoCodecCapability()` は、
`VplVideoEncoder::IsSupported()` と `VplVideoDecoder::IsSupported()` の結果を能力に反映する。

- `src/hwenc_vpl/vpl_video_encoder.cpp` の `VplVideoEncoder::IsSupported()` は
  `VplVideoEncoderImpl::CreateEncoder()` を 1920 x 1080 / 30 fps / 10 - 20 kbps の固定条件で呼ぶ。
- `src/hwenc_vpl/vpl_video_decoder.cpp` の `VplVideoDecoder::IsSupported()` は
  `VplVideoDecoderImpl::CreateDecoder()` を 4096 x 4096 と 2048 x 2048 の候補で呼ぶ。
- `VplVideoEncoderImpl::CreateEncoder()` は `Queries()` の結果で可否を決め、`init` が真なら `Init()` まで実行する。

つまり能力の有無を、実際のエンコード・デコードの初期化と同じ手順で判定している。
この方式は、固定した解像度・フレームレート・ビットレートの組み合わせに対して
VPL 実装が `MFX_ERR_UNSUPPORTED` を返す場合に、対応しているコーデックを非対応と判定する。

### 一覧と実際の可否が一致しないことを実機で確認した

`mfxImplDescription` のコーデック一覧は、その実装が扱えるコーデックを列挙する。
2026-10-17 に gmktec-k9 (libvpl 2.13) と k9s-2 (libvpl 2.16) で、
一覧の有無と実際の `Query()` の結果を突き合わせた。

デコード:

| コーデック | 一覧 | 実際の `Query()` |
| --- | --- | --- |
| AV1 / VP9 / AVC / HEVC / MPEG2 / JPEG | あり | 成功 |
| VP8 | あり | `MFX_ERR_UNSUPPORTED` |
| VC1 | あり | `MFX_ERR_UNSUPPORTED` |
| VVC | あり | `MFX_ERR_UNSUPPORTED` |

VP8 / VC1 / VVC は一覧に載っているが、初期化できない。
したがって一覧だけを根拠に能力を決めると、これらを対応として報告することになる。
一覧を採用する場合は、実際に初期化できるかという別の判定を組み合わせる必要がある。

### 0112 で確認した固定条件への依存

0112 の調査では、AV1 の照会に `CodecProfile` を設定しない状態で、
gmktec-k9 (libvpl 2.13) は `Query()` が成功し、k9s-2 (libvpl 2.16) は
`MFX_ERR_UNSUPPORTED` になることを確認した。
同じビルド・同じ解像度でも、VPL 実装のバージョンによって結果が変わる。

### 実デコード初期化が固定解像度と固定レベルを使う

`src/hwenc_vpl/vpl_video_decoder.cpp` の `VplVideoDecoderImpl::InitVpl()` は
`CreateDecoder()` を 4096 x 4096 と 2048 x 2048 の候補で呼び、`Init()` まで実行する。
AV1 は `MFX_LEVEL_AV1_2` を固定で指定している。

`Configure()` は `settings.max_render_resolution()` を保持するだけで、
受信するビットストリームの実際の解像度は `Decode()` の中で `GetVideoParam()` を呼ぶまで分からない。

### アダプターの指定ができない

vpl-rs の `AdapterSelector` は、複数の Intel GPU がある環境でどの物理アダプターを使うかを
`/dev/dri/renderD<N>` の番号で指定する。
`supported_codecs()` は `mfxImplDescription.Impl` と `mfxExtendedDeviceId.DRMRenderNodeNum` の
両方でローダーを絞り込み、能力検出と実際の生成で同じアダプターを使う。

C++ SDK の `VplSession::Create()` は `mfxImplDescription.Impl` だけで絞り込むため、
アダプターの指定という概念がない。
gmktec-k9 と k9s-2 はいずれも `/dev/dri/renderD128` のみを持つ構成のため、
複数アダプター環境で能力検出と実際の生成が別のアダプターを選ぶかは未確認である。
該当する実機を用意できない場合は、この項目は未検証として記録する。

### 0112 の修正で残った問題

0112 では AV1 デコーダーの `CodecProfile` の指定漏れを修正した。
`src/hwenc_vpl/vpl_video_encoder.cpp` の `VplVideoEncoderImpl::Queries()` でも
AV1 の `CodecProfile` が指定されていないが、`VplVideoEncoder::IsSupported()` は
`init` が偽の状態で `Queries()` を呼ぶため、0112 では変更していない。
エンコード側で同じ指定漏れが問題になるかは、本 issue で確認する。

### 既存 issue との関係

0112 は AV1 の `CodecProfile` の指定漏れを修正して closed にした。
本 issue はその修正では扱わなかった能力検出と初期化の構造を対象とする。
0076 の AV1 / H.265 のサイマルキャスト問題とは発生段階が異なる。
open / closed / pending の issue、CHANGES.md、直近のコミット履歴に、
同じ構造を扱った記録は見つからなかった。

## 設計方針

Rust SDK の実装は参考情報として扱い、C++ SDK 側で実機検証したうえで採否を決める。
以下は検証する候補であり、実装時に効果と影響を確認してから採用する。

### 能力検出の判定方法

能力検出が固定解像度の生成に依存しないようにする。
候補として、`mfxImplDescription` の一覧を参照する方式を検証する。

一覧だけでは VP8 / VC1 / VVC のように実際には初期化できないコーデックを対応と報告するため、
一覧と実際の可否を組み合わせる方法を検討する。
例えば一覧に載っているコーデックについてのみ実際の生成を試す、逆に実際の生成を試す条件から
解像度への依存を外す、といった案を実機で比較する。

能力検出と実際のエンコーダー・デコーダー生成で、同じセッションとアダプターを使うようにする。

### 実デコード初期化

固定解像度と固定レベルへの依存を見直す。
候補として、最初の受信ビットストリームを `MFXVideoDECODE_DecodeHeader()` で解析してから
`Init()` を呼ぶ方式を検証する。

この方式を採る場合、`Configure()` では初期化せず、最初の `Decode()` で初期化する順序になる。
`Configure()`、`Decode()`、`Release()` の責務とリソース管理をその順序に合わせ、
ヘッダーの不足や初期化の失敗をエラーとして安全に扱う。

### アダプター指定

複数 GPU 環境での必要性を判断する。
該当する実機を用意できない場合は、未検証であることを記録したうえで、
`VplSession::Create()` の絞り込み条件と能力検出・生成の整合だけを確認する。

### エンコード側の CodecProfile

`VplVideoEncoderImpl::Queries()` の AV1 の `CodecProfile` の指定漏れが、
デコーダーと同様に問題となるかを実機で確認する。
問題となる場合は、デコーダーと同じく `CodecProfile` を指定する。

### E2E

`e2e-test/test_sumomo_intel_vpl.py` は、能力検出の変更によって実行対象が変わらないようにする。
検出した対応能力に応じてケースを選ぶ場合でも、能力検出が対応を報告した方向で
起動・初期化・送受信に失敗した場合はテストを失敗させる。
非対応と判定されたことを理由に skip して、失敗を隠さない。

0112 で、E2E を能力別に書き換えた結果として、本来失敗するケースが skip に変わり
問題の解決を判断できなくなった経緯がある。
能力別のケースを追加する場合も、既存の 12 ケースの検証内容は維持する。

## テスト戦略

- 変更の前後で、gmktec-k9 と k9s-2 の `sumomo --show-video-codec-capability` の出力を記録し、
  コーデックごとの対応の変化を確認する。特に VP8 / VC1 / VVC が対応として報告されないことを確認する。
- 能力検出が対応を報告したコーデックについて、実際の送受信が成功することを確認する。
  0112 で使用した `test_sumomo_intel_vpl.py` の 12 ケースを実行し、回帰がないことを確認する。
- 能力検出が対応を報告した方向での起動・初期化・送受信の失敗が、skip ではなくテスト失敗となることを確認する。
- VPL API の能力取得エラーや VPL の利用不可は、理由を示して skip となることを確認する。
- 実機での確認は CODEBASE.md の E2E 実行規約に従い、対象のテストケースを単独で実行する。
- 複数 GPU 環境が必要な項目は、該当する実機を用意できない場合は未検証として記録し、
  モックや能力結果の偽装で検証済みとしない。

## 完了条件

- 能力検出が固定解像度での生成に依存せず、一覧に載っているが実際には初期化できないコーデックを
  対応として報告しない。VP8 / VC1 / VVC について、実機で確認した結果が記録されている。
- 能力検出が対応を報告したコーデックで、実際の送受信が成功する。
- 実デコード初期化が受信ビットストリームに基づくパラメーターを使用し、固定解像度と固定レベルへの
  依存が解消されている。採用しなかった場合は、その理由が記録されている。
- 0112 の修正で確認した 12 ケースに回帰がない。
- 能力検出が対応を報告した方向での失敗が skip に変えられず、テスト失敗として扱われる。
- エンコード側の AV1 の `CodecProfile` の指定漏れが問題となるかを実機で確認し、判断と根拠が記録されている。
- アダプター指定の要否について、判断と根拠が記録されている。未検証の項目は未検証と明記されている。

## 解決方法

{どのように対応するのかを明確にすること}
