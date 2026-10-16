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

## コメントの規約

コメントは今のソースコードの性質だけを書くこと。

- コメントは、今のコードに実在するもの (コードの行やシンボル、呼び出している外部 API、根拠となる仕様) を対象にすること。採用しなかった案、消した値、変更予定、過去の状態のように、今のコードに無いものを対象にしないこと
- 変更履歴を書かないこと。「以前は」「従来は」「旧実装では」「もともと」「かつて」のように、今のコードを読んでも確認できない過去の状態を説明してはいけない
- バグ修正の経緯や差分の理由を書かないこと。修正内容そのものは今のコードと `CHANGES.md` が示す
- 「なぜそう書いていないか」を書く場合も、今のコードから読み取れる制約だけを根拠にすること
- モジュールの責務を越えた説明を書かないこと。あるモジュールが知り得ない別モジュールの実装 (具体名・広告内容・分岐など) をそのモジュールのコメントに書いてはいけない

`///` や `//!`、`/** */` のように外部に公開されるコメントは、その外部仕様を主として説明すること。
可視性にかかわらず、その項目を利用する側 (クレート内の呼び出し元も含む) から見た仕様を書く。

- 外部仕様とは、入力と前提、戻り値・エラー・`None` の条件、順序、有効期間、副作用などの契約を指す
- 内部動作 (呼び出している外部 API、処理の順序、データ構造の選択) は補足としてのみ使い、仕様の説明の主軸としないこと
- 内部動作が戻り値の意味や順序を決めている場合は、その内部動作が外部仕様であるため、そのまま外部仕様として書いてよい
- 主軸にしない内部動作の理由は、doc ではなくコード行の `//` コメントに書くこと

例えば「`VTCopySupportedPropertyDictionaryForEncoder` を指定した解像度で呼び出す」ではなく
「指定したコーデック・解像度で選ばれるエンコーダーの ID と、そのエンコーダーのサポートプロパティ辞書を返す」と書く。

バグ修正のコメントは「今のコードがなぜこの順序・この条件でなければならないか」を書く。
例えば「この検証を先に行うのは、後続の処理がこの値を JSON 表現に変換するためであり、JSON 表現を持たない値が先に進むと panic するためである」のように、今のコードの依存関係として説明する。

## 作業ツリーの扱い

作業ツリーはエージェントだけのものではない。利用者が直接編集している可能性を常に考慮すること。

- 作業を始める前に `git status` と `git diff` で作業ツリーの現状を確認し、前回の自分の作業では説明できない変更があれば利用者の編集とみなすこと
- 利用者の編集と思われる変更を見つけたら、勝手に上書き・削除・取り消しをせず、まずその内容を報告して扱いを確認すること
- 作業ツリーを変更する操作の前に、対象ファイルをリポジトリ外へ退避すること。利用者の編集は Git オブジェクトになっていないことがあり、作業ツリーを上書きすると復元できない
- `git checkout -- <path>` / `git restore <path>` / `git reset --hard` / `git stash` で作業ツリーを戻してよいと判断しないこと。「戻せる」という前提自体が誤りである
- 作業の後は、退避した内容と現在のファイルを `diff` で突き合わせ、意図した変更だけが入っていることを確認すること
- `git commit --amend` は index 全体を巻き込むため、実行前に `git diff --cached` で意図したファイルだけがステージされていることを確認すること
- コミットの後は `git status` と `git diff HEAD` が空であることを確認すること。空でなければ commit が作業ツリーを取りこぼしている
