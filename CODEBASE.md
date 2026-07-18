# sora-cpp-sdk

## ビルド

ビルドはすべてリポジトリのルートディレクトリから実行する。

### 通常ビルド (Sora C++ SDK 本体)

```bash
python3 run.py build <target>
```

#### 対応ターゲット

- `windows_x86_64`
- `macos_arm64`
- `ubuntu-22.04_x86_64`
- `ubuntu-24.04_x86_64`
- `ubuntu-26.04_x86_64`
- `ubuntu-22.04_armv8`
- `ubuntu-24.04_armv8`
- `ubuntu-26.04_armv8`
- `raspberry-pi-os_armv8`
- `ios`
- `android`

#### 主なオプション

- `--debug` : Debug ビルド
- `--relwithdebinfo` : RelWithDebInfo ビルド
- `--disable-cuda` : CUDA を無効化
- `--test` : テストをビルド対象に含める
- `--run-e2e-test` : E2E テストを実行する
- `--package` : リリースパッケージを生成する
- `--local-webrtc-build-dir <dir>` : ローカルの webrtc-build を参照する (指定時は webrtc-build もビルドされる)
- `--local-webrtc-build-args <args>` : ローカル webrtc-build のビルド引数

#### 例

```bash
# macOS arm64 でリリースビルド
python3 run.py build macos_arm64

# Ubuntu 24.04 x86_64 でデバッグビルド + テスト
python3 run.py build ubuntu-24.04_x86_64 --debug --test

# パッケージ生成まで行う
python3 run.py build ubuntu-24.04_x86_64 --package
```

### サンプル (examples) のビルド

各サンプルのディレクトリに `run.py` がある。サンプルごとに同じ要領でビルドする。

```bash
python3 examples/<sample>/run.py build <target>
```

#### サンプル一覧

- `examples/sumomo` : Momo の sora モードを模したサンプル
- `examples/sdl_sample` : SDL で受信映像を表示するサンプル
- `examples/messaging_recvonly_sample` : Sora のメッセージング機能でメッセージを受信するサンプル

#### サンプルの対応ターゲット

iOS と Android を除いた以下のターゲットに対応する。

- `windows_x86_64`
- `macos_arm64`
- `ubuntu-22.04_x86_64`
- `ubuntu-24.04_x86_64`
- `ubuntu-26.04_x86_64`
- `ubuntu-22.04_armv8`
- `ubuntu-24.04_armv8`
- `ubuntu-26.04_armv8`
- `raspberry-pi-os_armv8`

#### サンプル固有の主なオプション

- `--debug` : Debug ビルド
- `--install-dir <dir>` : インストール先ディレクトリを指定する
- `--local-sora-cpp-sdk-dir <dir>` : ローカルの Sora C++ SDK を参照する (指定時は Sora C++ SDK もビルドされる)
- `--local-sora-cpp-sdk-args <args>` : ローカル Sora C++ SDK のビルド引数
- `--local-webrtc-build-dir <dir>` : ローカルの webrtc-build を参照する
- `--local-webrtc-build-args <args>` : ローカル webrtc-build のビルド引数

#### 例

```bash
# sumomo を macOS arm64 でビルド
python3 examples/sumomo/run.py build macos_arm64

# ローカルの Sora C++ SDK を参照しながら sumomo をビルド
python3 examples/sumomo/run.py build ubuntu-24.04_x86_64 \
    --local-sora-cpp-sdk-dir .
```

## E2E テスト

E2E テストは `e2e-test/` 以下に配置されているが、実行はリポジトリのルートディレクトリから `--directory=e2e-test` を指定して `uv` 経由で行う。

### 基本的な実行方法

特定のテストケースのみを実行する。全テストを一括実行しない。`-v -s` と `--timeout` を必ず指定する。タイムアウトは最大 60 秒とする。

```bash
uv run --directory=e2e-test pytest <test_file>::<test_case> -v -s --timeout=60
```

### ハードウェアアクセラレーター対応

検証するハードウェアアクセラレーターに合わせて環境変数を指定する。

| 環境変数 | 検証対象 | 主なテストファイル |
| --- | --- | --- |
| `INTEL_VPL=1` | Intel VPL | `test_sumomo_intel_vpl.py` |
| `NVIDIA_VIDEO_CODEC=1` | NVIDIA Video Codec | `test_sumomo_nvidia_video_codec.py` |
| `AMD_AMF=1` | AMD AMF | `test_sumomo_amd_amf.py` |
| `RASPBERRY_PI=1` | Raspberry Pi V4L2 M2M | `test_sumomo_raspberry_pi.py` |
| `APPLE_VIDEO_TOOLBOX=1` | Apple Video Toolbox | `test_sumomo_apple_video_toolbox.py` |

ハードウェアアクセラレーター不要のテストは以下を利用する。

- `test_sumomo_basic.py` : 基本動作
- `test_sumomo_openh264.py` : Cisco OpenH264

### 例

```bash
# AMD AMF の H.264 sendonly/recvonly テスト
AMD_AMF=1 uv run --directory=e2e-test pytest \
    test_sumomo_amd_amf.py::test_sendonly_recvonly[H264] -v -s --timeout=60

# Apple Video Toolbox の H.265 テスト
APPLE_VIDEO_TOOLBOX=1 uv run --directory=e2e-test pytest \
    test_sumomo_apple_video_toolbox.py::test_sendonly_recvonly[H265] -v -s --timeout=60

# 基本テストの特定ケース
uv run --directory=e2e-test pytest \
    test_sumomo_basic.py::test_sendonly_recvonly[VP8] -v -s --timeout=60
```
