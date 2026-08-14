# VPL_CHECK_RESULT マクロが throw した例外を catch するコードが不在

- Priority: High
- Created: 2026-07-10
- Completed: 2026-07-13
- Model: DeepSeek V4 Pro
- Branch: feature/fix-vpl-check-result-throw-catch
- Polished: 2026-07-10
## 目的

`src/hwenc_vpl/vpl_utils.h` の `VPL_CHECK_RESULT` マクロが `mfxStatus` の値を `throw` するが、VPL エンコーダ/デコーダ実装 (`src/hwenc_vpl/vpl_video_encoder.cpp`, `src/hwenc_vpl/vpl_video_decoder.cpp`) の全コールサイトに対応する `catch` ブロックが存在しない (`grep -rn "catch" src/hwenc_vpl/` の結果は 0 件)。例外が捕捉されないまま WebRTC コーデックフレームワーク側へ伝播すると `std::terminate()` が呼ばれプロセスがクラッシュする。WebRTC コーデックフレームワークは戻り値によるエラーハンドリングを前提としているため、throw ではなく戻り値でエラーを返すように修正する。

## 優先度根拠

VPL ハードウェアエンコーダ/デコーダ使用時にハードウェアエラーが発生するとプロセスが強制終了する。VPL を利用する全環境で発生しうる致命バグであり High。

## 現状

`vpl_utils.h:14-20` の実際のマクロ定義:

```cpp
#define VPL_CHECK_RESULT(P, X, ERR)                    \
  {                                                    \
    if ((X) > (P)) {                                   \
      RTC_LOG(LS_ERROR) << "Intel VPL Error: " << ERR; \
      throw ERR;                                       \
    }                                                  \
  }
```

引数は 3 個で、`P` が実際の結果値、`X` が比較閾値、`ERR` がログ出力かつ throw する値である。全コールサイトは `VPL_CHECK_RESULT(sts, MFX_ERR_NONE, sts)` という同一パターンで呼ばれており、`if ((MFX_ERR_NONE) > (sts))`、すなわち `sts < 0` (エラー) の場合に `sts` (`mfxStatus`) を throw する。`MFX_WRN_*` 系の警告 (`sts > 0`) は throw されず通過する点に注意する。

コールサイトはすべて例外を捕捉していない。各コールサイトの所属関数と戻り値型は以下の通りで、戻り値型が一様ではない:

| ファイル:行 | 関数 | 戻り値型 |
| --- | --- | --- |
| `vpl_video_encoder.cpp:536` | `VplVideoEncoderImpl::Encode()` | `int32_t` |
| `vpl_video_encoder.cpp:555` | `VplVideoEncoderImpl::Encode()` | `int32_t` |
| `vpl_video_encoder.cpp:575` | `VplVideoEncoderImpl::Encode()` | `int32_t` |
| `vpl_video_encoder.cpp:578` | `VplVideoEncoderImpl::Encode()` | `int32_t` |
| `vpl_video_encoder.cpp:752` | `VplVideoEncoderImpl::InitVpl()` | `int32_t` |
| `vpl_video_encoder.cpp:758` | `VplVideoEncoderImpl::InitVpl()` | `int32_t` |
| `vpl_video_decoder.cpp:191` | `VplVideoDecoderImpl::CreateDecoderInternal()` | `std::unique_ptr<MFXVideoDECODE>` |
| `vpl_video_decoder.cpp:306` | `VplVideoDecoderImpl::Decode()` | `int32_t` |
| `vpl_video_decoder.cpp:309` | `VplVideoDecoderImpl::Decode()` | `int32_t` |

特に `vpl_video_decoder.cpp:191` は `CreateDecoderInternal()` 内にあり、戻り値型が `std::unique_ptr<MFXVideoDECODE>` である。この関数は既に他のエラー経路 (`vpl_video_decoder.cpp:177,205`) で `return nullptr` を用いており、ここで `WEBRTC_VIDEO_CODEC_ERROR` (int) を返すことはできない。また `CreateDecoderInternal()` は `CreateDecoder()` (`vpl_video_decoder.cpp:103`) 経由で `VplVideoDecoder::IsSupported()` (`vpl_video_decoder.cpp:405`, 戻り値型 `bool`) からも呼ばれるため、現状ではハードウェア能力検査中の throw でクラッシュしうる。`return nullptr` に修正すれば `CreateDecoder()` → `IsSupported()` の既存 nullptr ハンドリングを通じて `false` が返り、この経路も同時に解消される。

## 設計方針

`VPL_CHECK_RESULT` を throw ではなく戻り値でエラーを返す形に変更する。コールサイトの戻り値型が一様でないため、マクロに戻り値を渡す引数 `RET` を追加し、`RTC_LOG` によるログ出力は維持する:

```cpp
#define VPL_CHECK_RESULT(P, X, ERR, RET)               \
  {                                                    \
    if ((X) > (P)) {                                   \
      RTC_LOG(LS_ERROR) << "Intel VPL Error: " << ERR; \
      return RET;                                      \
    }                                                  \
  }
```

この形式は `sts < 0` のみを捕捉する既存の判定 (`MFX_WRN_*` 警告は通過) をそのまま維持する。各コールサイトは第 4 引数に所属関数の戻り値型に合った値を渡すよう書き換える:

- `int32_t` を返す `Encode()` / `InitVpl()` / `Decode()` のコールサイト (encoder:536,555,575,578,752,758, decoder:306,309): `VPL_CHECK_RESULT(sts, MFX_ERR_NONE, sts, WEBRTC_VIDEO_CODEC_ERROR)`
- `std::unique_ptr<MFXVideoDECODE>` を返す `CreateDecoderInternal()` のコールサイト (decoder:191): `VPL_CHECK_RESULT(sts, MFX_ERR_NONE, sts, nullptr)`

`Decode()` のコールサイト (decoder:306,309) は `while (true)` ループ (`vpl_video_decoder.cpp:266-`) の内側にある。`return RET` はループを抜けて `Decode()` 自体を終了させるが、これは throw 版でも例外がループを抜けて関数を脱出していたのと同じ挙動であり意図通りである。なお throw 版では `sts < 0` の際に `GetVideoParam()` (`vpl_video_decoder.cpp:283`) 実行前に例外脱出していたが、return 版では判定が同じ行 (306) にあるため実行順序は変わらない。

## 完了条件

- VPL エンコーダ/デコーダでハードウェアエラー (`sts < 0`) 発生時に throw されず、所属関数が戻り値でエラーを返すこと。`std::terminate()` が呼ばれないこと
- `VPL_CHECK_RESULT` マクロが throw ではなく戻り値 (`RET` 引数) を return すること
- 各コールサイトが所属関数の戻り値型に合った値を返すこと (`int32_t` 関数は `WEBRTC_VIDEO_CODEC_ERROR`、`CreateDecoderInternal()` は `nullptr`)
- `src/hwenc_vpl/` に throw / catch が残らないこと (`grep -rn "throw\|catch" src/hwenc_vpl/` が 0 件)
- Intel VPL 実機環境で `INTEL_VPL=1 uv run --directory=e2e-test pytest test_sumomo_intel_vpl.py -v -s --timeout=60` の正常系が退化しないこと (ハードウェアエラーの意図的な注入は困難なため、自動テストは正常系維持の確認に留める)
- `CHANGES.md` の `## develop` 配下、`### misc` セクションに `[FIX]` エントリを追記する:
  ```
  - [FIX] VPL_CHECK_RESULT マクロが throw した例外が catch されずプロセスがクラッシュしうる問題を修正する
    - @<担当者>

## 解決方法

- `VPL_CHECK_RESULT` マクロに第 4 引数 `RET` を追加し、`throw ERR` を `return RET` に変更した
- `src/hwenc_vpl/vpl_video_encoder.cpp` の 6 箇所のコールサイトに `WEBRTC_VIDEO_CODEC_ERROR` を第 4 引数として追加した
- `src/hwenc_vpl/vpl_video_decoder.cpp` の `CreateDecoderInternal()` 内のコールサイトには `nullptr` を、`Decode()` 内の 2 箇所には `WEBRTC_VIDEO_CODEC_ERROR` を第 4 引数として追加した
- 修正後 `grep -rn "throw\|catch" src/hwenc_vpl/` が 0 件であることを確認した
  ```
