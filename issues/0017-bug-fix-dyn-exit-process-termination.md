# DYN_REGISTER マクロが exit(1) でプロセスを強制終了する

- Priority: High
- Created: 2026-07-10
- Completed: {YYYY-MM-DD}
- Model: DeepSeek V4 Pro
- Branch: feature/fix-dyn-exit-process-termination
- Polished: 2026-07-10

## 目的

`include/sora/dyn/dyn.h` の `DYN_REGISTER` マクロが動的ライブラリの関数解決失敗時に `exit(1)` を呼び、プロセス全体を強制終了する。 SDK ライブラリがプロセスを終了させることは許容されない。 CUDA/NvCodec の動的ロードに失敗した場合にアプリケーション全体が問答無用でクラッシュするライブラリとしての致命的な設計問題であり、修正が必要。

## 優先度根拠

- CUDA/NvCodec の動的ロードに失敗した場合にアプリケーション全体が `exit(1)` でクラッシュする
- SDK ライブラリが呼び出し元プロセスを自発的に終了させることはライブラリの責務を逸脱している
- `exit(1)` が呼ばれているため、呼び出し元の `try/catch` が一切機能していない
- `CudaContext::Create()` は `try/catch` を用意しているが、`exit(1)` により無意味になっている
- High とした理由: 実環境で通常発生しうるエラー（CUDA 非搭載環境での SDK ロード）でプロセスが強制終了されるため

## 現状

`include/sora/dyn/dyn.h:107-119` の `DYN_REGISTER` マクロ:

```cpp
#define DYN_REGISTER(soname, func)                                             \
  template <class... Args>                                                     \
  inline auto func(Args... args) {                                             \
    typedef std::add_pointer<decltype(::func)>::type func_type;                \
    auto f =                                                                   \
        (func_type)DynModule::Instance().GetFunc(soname, DYN_STRINGIZE(func)); \
    if (f == nullptr) {                                                        \
      std::cerr << "Failed to GetFunc: " << DYN_STRINGIZE(func)                \
                << " soname=" << soname << std::endl;                          \
      exit(1);                                                                 \
    }                                                                          \
    return f(args...);                                                         \
  }
```

`dyn.h:4` で `#include <iostream> // IWYU pragma: export` により `<iostream>` が全利用者に強制伝播している。これは `std::cerr` 出力のためだけに使われており、大規模ヘッダである `<iostream>` を全翻訳単位に注入する副作用がある。

`DYN_REGISTER` の利用箇所:
- `include/sora/dyn/cuda.h`: `cuInit`, `cuDeviceGet`, `cuDeviceGetCount`, `cuDeviceGetName`, `cuCtxCreate`, `cuCtxDestroy`, `cuCtxPushCurrent`, `cuCtxPopCurrent`, `cuGetErrorName`, `cuMemAlloc`, `cuMemAllocPitch`, `cuMemFree`, `cuMemcpy2D`, `cuMemcpy2DAsync`, `cuMemcpy2DUnaligned`, `cuStreamSynchronize`, `cuStreamCreate` (17 関数)
- `include/sora/dyn/nvcuvid.h`: `cuvidCreateDecoder`, `cuvidReconfigureDecoder`, `cuvidDestroyDecoder`, `cuvidDecodePicture`, `cuvidGetDecodeStatus`, `cuvidGetDecoderCaps`, `cuvidCreateVideoParser`, `cuvidDestroyVideoParser`, `cuvidParseVideoData`, `cuvidMapVideoFrame`, `cuvidUnmapVideoFrame`, `cuvidCtxLockCreate`, `cuvidCtxLockDestroy` (13 関数)

DYN_REGISTER 関数の呼び出し元と既存の例外保護状況:

| ファイル:行 | 関数 | 既存の catch | 修正要否と理由 |
|---|---|---|---|
| `cuda_context_cuda.cpp:51-74` | `CudaContext::Create()` | `catch(std::exception&)` | 不要 (`std::runtime_error` を捕捉可能) |
| `cuda_context_cuda.cpp:77-104` | `CudaContext::CanCreate()` | なし | **要修正**: try/catch 追加し false を返す |
| `nvcodec_video_encoder.cpp:674-737` | `IsSupported()` | `catch(const NVENCException&)` | 実質安全。`GetFunc` で事前チェック後にエンコーダを試行するため |
| `nvcodec_video_encoder.cpp:594-599` | `CreateEncoder()` (Linux) | `catch(const NVENCException&)` | **要注意**: `std::runtime_error` を捕捉できないが、`GetFunc` 事前チェック通過後のため実質的に到達しない |
| `nvcodec_video_encoder.cpp:363-368` | `Encode()` の `cuda_->Copy()` | `catch(const NVENCException&)` | **要注意**: エンコード中に動的ライブラリが再配置されることはないが、catch の広さが不十分 |
| `nvcodec_video_decoder.cpp:155-158` | `InitNvCodec()` | なし | **要修正**: `NvCodecDecoderCuda` コンストラクタからの例外を捕捉する try/catch を追加 |
| `nvcodec_video_decoder.cpp:41-71` | `IsSupported()` | `catch(...)` | 不要（全例外を捕捉可能、かつ事前 `GetFunc` チェックあり） |
| `nvcodec_video_codec_cuda.cpp:15-28` | `GetNvCodecGpuDeviceName()` | なし | **要修正**: try/catch 追加。void 戻り値のため、呼び出し元が失敗を検知できない問題もあり |
| `nvcodec_video_encoder.cpp:685-698` | 初期化コード (Linux) | 戻り値チェック | 不要（明示的な `IsLoadable` + `GetFunc` チェックでカバー） |
| `nvcodec_video_decoder.cpp:48-62` | 初期化コード | `catch(...)` | 不要（`IsSupported()` 内で try/catch + 事前チェック） |
| `nvcodec_video_encoder_cuda.cpp:56-150` | `ShowEncoderCapability()` | なし | **要対応**: デッドコードだが独自の `exit(1)` がある。本修正では削除せず、独自 `exit(1)` の存在を確認のみ |
| `third_party/NvCodec/NvCodec/NvDecoder/NvDecoder.cpp:918-940` | `~NvDecoder()` | デストラクタ内に try/catch なし | **要注意**: デストラクタからの例外は `std::terminate()` につながる。 sora-cpp-sdk 側で `NvCodecDecoderCuda` のデストラクタが `NvDecoder` 破棄前に `DynModule` の生存を保証する設計になっているか検証する |
| `third_party/NvCodec/NvCodec/NvEncoder/NvEncoderCuda.cpp:114-138` | `ReleaseCudaResources()` | なし | **要注意**: `NvEncoderCuda` デストラクタから呼ばれる。`NvCodecVideoEncoderCuda::~NvCodecVideoEncoderCuda()` が `NvEncoderCuda` より先に破棄される設計なら安全だが要検証 |

## 設計方針

1. `exit(1)` を `throw std::runtime_error` に置き換える
2. エラー情報（関数名と soname）は例外メッセージに含める。呼び出し元が `what()` で原因を特定できるようにする
3. `std::cerr` 出力を削除する（エラー情報は例外に含めるため）
4. `#include <iostream>` と `// IWYU pragma: export` を削除する
5. `#include <stdexcept>` を追加する（`std::runtime_error` に必要）
6. `CudaContext::CanCreate()` に `try/catch(const std::exception&)` を追加し、失敗時に `false` を返すようにする。既存の戻り値チェック（ `if (r != CUDA_SUCCESS)` ）は try ブロック内に残し、DYN_REGISTER の throw と CUDA API エラーの両方をカバーする
7. `GetNvCodecGpuDeviceName()` に `try/catch(const std::exception&)` を追加する。戻り値型は void のまま、失敗時は呼び出し元が空文字列の name を前提に処理する（現状そうなっていることを確認する）
8. `NvCodecVideoDecoder::InitNvCodec()` に `try/catch(...)` を追加し、`NvCodecDecoderCuda` コンストラクタからの例外を捕捉する
9. `third_party/NvCodec` 配下のデストラクタ経路については、sora-cpp-sdk 側のラッパーデストラクタが先に破棄を完了させることで例外伝播を防ぐ設計になっているか検証する。必要であれば適切に保護する
10. ログ出力については、`dyn.h` は公開ヘッダーであるため `rtc_base/logging.h` への依存を追加しない。エラー情報は例外メッセージに含め、呼び出し元が適切なログ機構で出力する責務とする

## 完了条件

### dyn.h の修正

- [ ] `DYN_REGISTER` マクロで関数解決失敗時に `exit(1)` が呼ばれないこと
- [ ] 関数解決失敗時に `std::runtime_error` が throw されること
- [ ] 例外メッセージに `"Failed to GetFunc: <関数名> soname=<soname>"` が含まれること（`what()` で取得可能であること）
- [ ] 例外メッセージ構築は `std::string` 経由で行い、`const char* + const char*` によるコンパイルエラーが発生しないこと
- [ ] `#include <stdexcept>` が追加されていること
- [ ] `std::cerr` 出力が削除されていること
- [ ] `#include <iostream>` と `// IWYU pragma: export` が削除されていること
- [ ] `rtc_base/logging.h` への依存が追加されていないこと

### 呼び出し元の修正

- [ ] `CudaContext::CanCreate()` に `try/catch(const std::exception&)` を追加し、例外発生時に `false` を返すこと。既存の `if (r != CUDA_SUCCESS)` による戻り値チェックは try ブロック内に残すこと
- [ ] `GetNvCodecGpuDeviceName()` に `try/catch(const std::exception&)` を追加すること
- [ ] `NvCodecVideoDecoder::InitNvCodec()` に `try/catch(...)` を追加し、失敗時に `false` を返すこと

### 検証

- [ ] `src/cuda_context_cuda.cpp` の `Create()` が動作していることを確認する（既存の try/catch が正しく例外を捕捉する）
- [ ] `#include <iostream>` 削除後に全プラットフォーム（Ubuntu, macOS, Windows, iOS, Android）でビルドが通ることを確認する
- [ ] 設計方針 9 の検証: `third_party/NvCodec` のデストラクタからの例外伝播がないことを確認する
- [ ] `nvcodec_video_encoder_cuda.cpp:56-150` の `ShowEncoderCapability()`（デッドコード）内の独自 `exit(1)` が残存していること、および本修正によるビルドエラーが発生しないことを確認する

### 変更履歴

- [ ] `CHANGES.md` の `## develop` 直下（`### misc` セクションより前）に `[FIX]` エントリを追記する:

```
- [FIX] include/sora/dyn/dyn.h の DYN_REGISTER マクロが exit(1) でプロセスを強制終了するのを修正する
  - exit(1) を throw std::runtime_error に置き換える
  - エラー情報を例外メッセージに含める
  - std::cerr 出力を削除し <iostream> の依存を除去する
  - CudaContext::CanCreate() に try/catch を追加する
  - GetNvCodecGpuDeviceName() に try/catch を追加する
  - NvCodecVideoDecoder::InitNvCodec() に try/catch を追加する
  - @<担当者>
```

### テスト

- [ ] `DYN_REGISTER` マクロで関数解決失敗時に `std::runtime_error` が throw されることを検証する Catch2 ユニットテストを `test/` ディレクトリに追加する
  - テスト方針: 存在しない soname を指定して `GetFunc` が nullptr を返す状況を作り出す
  - 動的ライブラリの mock や stub は使用しない（AGENTS.md 規約）
  - CUDA 非搭載環境ではテストを自動スキップする仕組みを入れる
- [ ] `CudaContext::CanCreate()` が例外捕捉時に `false` を返すことを検証するテストを追加する
