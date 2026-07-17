# multistrap を廃止し apt-get + dpkg-deb による sysroot 構築に置き換える

- Priority: Medium
- Created: 2026-06-08
- Completed: {YYYY-MM-DD}
- Model: DeepSeek V4 Pro
- Branch: feature/change-replace-multistrap-with-sysroot
- Polished: 2026-07-17

## 目的

`multistrap` は Debian unstable から 2025-01-24 に削除され、Ubuntu でも 25.04 (plucky) 以降で消えているため、代替手段として `apt-get` + `dpkg-deb` を直接利用した sysroot 構築方式に移行する。実装は webrtc-build (`shiguredo-webrtc-build/webrtc-build`) の `feature/sysroot` ブランチのコミット `2c15196` (`ARM 向け sysroot 生成を独自実装へ移行する`) 時点の `sysroot_builder.py` を移植し、両プロジェクトで sysroot ビルド基盤を統一する。以降本文で「参照コミット」と表記した場合はこの `2c15196` を指す (gc による消失リスクへの対策は「完了条件」の「PR マージ後の後始末」で規定)。

本 issue は本体 (リポジトリルート) とサンプル (`examples/`) の両方の ARM64 クロスコンパイル用 sysroot 構築を対象とする。両者はパッケージ構成が異なる (後述) ため別々の設定ファイル群として扱う。副次的に、multistrap 分岐に残っていた Jetson 系の到達不能な dead code (`install_deps()` の 2 ターゲット、`buildbase.py` の `libnvbuf_fdmap.so` symlink 補正) も削除する。これは multistrap 分岐と一体で消えるコードに限定し、`run.py` の他所に散在する Jetson 参照は本 issue のスコープ外とする。

**sysroot 生成の対応ホスト OS**: `sysroot_builder.py` はホストの `apt-get` と `dpkg-deb` を呼ぶため、Linux ホスト (Ubuntu 22.04 / 24.04) 限定の動作となる。macOS ホストには `dpkg-deb` が標準では入っていないため、macOS 上での ARM64 sysroot 生成は本 issue の対応範囲外とする (macOS 開発者は Linux ランナー上の CI に頼るか、`brew install dpkg` で追加した環境で使う)。

## 優先度根拠

現在の CI は Ubuntu 24.04 (noble) 上で実行しているため `multistrap` はまだ利用可能だが、今後 CI を Ubuntu 25.04 以降にアップグレードする際にブロッカーとなる。また Ubuntu 24.04 LTS の標準サポート終了 (2029 年 4 月) までに移行を完了する必要がある。ただし現時点で即座に CI が壊れているわけではないため High ではなく Medium とする。

## 現状

### multistrap 設定ファイル (本体・サンプルで別系統)

`multistrap` 設定ファイルは 2 系統存在する。本体用の `multistrap/*.conf` (`run.py` が参照) と、サンプル用の `examples/multistrap/*.conf` (`examples/*/run.py` が参照)。**両者はパッケージ構成が異なる** ため、移行後もパッケージ集合の差異を維持する。

本体用 `multistrap/*.conf` (3 ファイル):

| 設定ファイル | ターゲット OS | リポジトリ | セクション | 主なパッケージ |
|---|---|---|---|---|
| `multistrap/ubuntu-22.04_armv8.conf` | Ubuntu 22.04 (jammy) | ports.ubuntu.com (HTTP) | Ports | libstdc++-10-dev, libc6-dev, libxext-dev, libdbus-1-dev |
| `multistrap/ubuntu-24.04_armv8.conf` | Ubuntu 24.04 (noble) | ports.ubuntu.com (HTTP) | Ports | libstdc++-13-dev, libc6-dev, libxext-dev, libdbus-1-dev |
| `multistrap/raspberry-pi-os_armv8.conf` | Debian trixie | deb.debian.org (HTTP) | Deb | libc6-dev, libstdc++-14-dev, libasound2-dev, libpulse-dev, libudev-dev, libexpat1-dev, libnss3-dev, libxext-dev, libxtst-dev |
| (同上、2 セクション目) | Raspberry Pi OS trixie | archive.raspberrypi.org (HTTP) | Rasp | libcamera-dev |

サンプル用 `examples/multistrap/*.conf` (3 ファイル)。本体との差分のみ示す:

| 設定ファイル | 本体との差分 |
|---|---|
| `examples/multistrap/ubuntu-22.04_armv8.conf` | libstdc++ を `-10-dev` ではなく **`-11-dev`** にし、**`libudev-dev` と `libgles-dev` を追加** |
| `examples/multistrap/ubuntu-24.04_armv8.conf` | **`libudev-dev` と `libgles-dev` を追加** (libstdc++ は `-13-dev` で本体と同一) |
| `examples/multistrap/raspberry-pi-os_armv8.conf` | 本体と同一 (末尾改行の有無のみ) |

サンプルの `libudev-dev` はゲームパッド等の入力デバイス、`libgles-dev` は SDL3 経由の OpenGL ES 描画のために必要。

`raspberry-pi-os_armv8.conf` の `[Deb]` / `[Rasp]` セクションは `components` を省略しており、これは `multistrap` のデフォルト `main` を採る挙動である。ubuntu 系 conf は `components=main universe` (スペース区切り)。いずれの conf も `--no-auth` オプションで認証をスキップしている。この置き換えを機に HTTPS + GPG `signed_by` 検証に切り替え、insecure 動作は廃止する。

各 conf の `bootstrap` と `aptsources` は全ファイルで同一セクションを指しており、依存解決に使うリポジトリとパッケージ取得に使うリポジトリが分離されていない。したがって移行後に全リポジトリを一律に `sources.list` へ追加する方式でも挙動は変わらない。

### buildbase.py の install_rootfs

`buildbase.py` の `install_rootfs()` (`buildbase.py:1114-1159`) が `multistrap --no-auth -a arm64 -d <rootfs_dir> -f <conf>` を実行し、以下を行う:

1. `rm_rf(rootfs_dir)` による既存 rootfs のクリーンアップ
2. 絶対パスシンボリックリンクの相対パス化
3. Jetson 用 `libnvbuf_fdmap.so` シンボリックリンクの作成 (`buildbase.py:1142-1159`。ファイルが存在する場合のみ、全ターゲットで試行)

**`buildbase.py` の由来と改変方針**: `buildbase.py:1-9` のコメントに記載のとおり、このファイルは `melpon/buildbase` 上流からコピーされたテンプレートで、原則 curl で丸ごと上書き更新する運用。ただし現状既に `install_rootfs()` の Jetson 用 symlink 補正など sora-cpp-sdk 独自の差分が入っており、実運用としては curl 上書きではなくローカル改変で管理されている。本 issue でも上流に PR を送らず、sora-cpp-sdk のローカル差分として `install_sysroot()` を追加する方針を採る。

### 呼び出し元

`install_rootfs()` は以下の 4 ファイルから呼び出されている:

- `run.py` (本体、`multistrap/` を参照。import 40 / multistrap 分岐 222-239)
- `examples/sumomo/run.py` (`examples/multistrap/` を参照。import 29 / conf 57 / 呼び出し 67)
- `examples/sdl_sample/run.py` (同上 29 / 57 / 67)
- `examples/messaging_recvonly_sample/run.py` (**1 行ずれ** 29 / 56 / 66)

いずれのファイルも `install_rootfs_args = {"version": <md5>, "version_file": "<install_dir>/rootfs.version", "install_dir": ..., "conf": <path>}` の辞書を組み立てて `install_rootfs(**install_rootfs_args)` の形で呼んでいる。**新方式では `version` / `version_file` の生成をやめ、`rootfs.version` ファイル自体を廃止する** (キャッシュ判定は `sysroot_builder` 側 manifest fingerprint に一元化)。したがって呼び出し側の diff は「dict の廃止 + 新シグネチャでの直接呼び出し」となり、4 ファイル全てで同じパターンの書き換えが発生する (「設計方針 3.」参照)。

### Jetson ターゲット

`run.py` の `install_deps()` の条件分岐 (`run.py:222-239` の `if platform.target.package_name in (...)`) では `install_rootfs()` の呼び出し対象に `ubuntu-20.04_armv8_jetson` と `ubuntu-22.04_armv8_jetson` が含まれているが、対応する `multistrap` 設定ファイルは存在しない。これらのターゲットは `AVAILABLE_TARGETS` (`run.py:668-677`) にも含まれておらず、`argparse` の `choices` で弾かれるため到達不能な dead code である。過去に `8ab17cac 0040 Jetson に関するコードを削除する` (2024-06-06) で Jetson 対応が `support/jetson-jetpack-{5,6}` ブランチに切り出された際に残った参照。

`install_sysroot()` に対応する JSON も用意しないため、これらのターゲットを条件分岐に残すと存在しない JSON を参照して失敗する。したがって本 issue では **`install_deps()` の当該条件分岐からこの 2 ターゲットを削除** し、あわせて `install_rootfs()` 内の Jetson 用 symlink ロジック (`buildbase.py:1142-1159`) を (関数ごと削除するため) 削除する。削除に際してコメントによる注釈は残さない。

**スコープ限定**: 削除するのは上記 2 箇所のみである。`run.py` の他の Jetson 参照 (`platform.target.os in ("jetson", ...)` 等、複数箇所) はスコープ外とし、本 issue マージ後に別 refactor issue を open で起票する。

### CI での multistrap 使用箇所

- `.github/workflows/ci.yml` armv8 ビルドジョブ: install 行 (`ci.yml:260`) と insecure 許可の sed パッチ (`ci.yml:261-262`)
- `.github/workflows/release.yml` build-ubuntu ジョブ: 同上 (`release.yml:249-251`)
- `.github/workflows/release.yml` build-ubuntu-examples ジョブ: `multistrap` + `binutils-aarch64-linux-gnu` install (`release.yml:660`) と sed パッチ (`release.yml:661-662`)

sed パッチはいずれも `Acquire::AllowInsecureRepositories=true` を `multistrap` に注入するもの。HTTP リポジトリからの取得を許可するために入れられている。

### webrtc-build 側の参考実装 (参照コミット `2c15196`)

`shiguredo-webrtc-build/webrtc-build` の `feature/sysroot` ブランチ参照コミット時点に、`apt-get` + `dpkg-deb` を用いた sysroot 構築モジュール `sysroot_builder.py` (399 行、テスト付き) が実装済み。要点のみ列挙する:

- `SysrootConfig` / `RepositoryConfig` の frozen dataclass
- `load_sysroot_config(path)`: JSON を validation 付きでロード。`CONFIG_TOKEN_PATTERN` (`^[A-Za-z0-9._+:/-]+$`) で使用可能文字を制限し、`SysrootConfigError` で詳細な検証エラーを返す。**副作用**: このパターンは resolve 後の絶対パスも検査するため、`install_dir` 配下の親パスに空白や `()` を含むと failure する
- `build_sysroot(config, output_dir, force=False)`: `tempfile.TemporaryDirectory` で隔離環境を作り、`APT_CONFIG` 環境変数 + `_apt_options()` が返す `-o` オプション群 + `apt.conf` 内 `Dir::Etc::main "/dev/null";` / `Dir::Etc::parts "/dev/null";` の合わせ技でホスト `/etc/apt/apt.conf.d/` を完全無効化した状態で `apt-get update` → `apt-get --download-only --yes --no-install-recommends --no-install-suggests install` → `dpkg-deb --extract` → 後処理 → tempfile → rename で atomic install。`apt-get` は `sudo` なしで呼ばれる (隔離環境の状態ディレクトリはすべて `output_dir` 配下)
- `_apt_options()` が返す 8 項目には **`-o Dir::Etc::preferences=/dev/null` と `-o Dir::Etc::preferencesparts=/dev/null` が含まれる**。これは pin 設定を一律無視する挙動であり、二次対応 (pin) を実装するときに必ず解除が必要となる (「設計方針 1.」参照)
- `sysroot_config_fingerprint(config)`: 設定内容 + GPG キーの中身 (SHA256) を合成した SHA256 fingerprint。sysroot ルートに `.webrtc-build-sysroot.json` (manifest) として保存
- 後処理: 絶対パス symlink → 相対パスへの補正、`bin`/`sbin`/`lib`/`lib64` の usrmerge symlink 補完、`usr/share/pkgconfig/{name}` から `../../lib/<triplet>/pkgconfig/{name}` を指す symlink 作成
- 既存 `output_dir` が存在し manifest 不一致・欠落の場合は `force=False` だと `SysrootBuildError` を送出。fingerprint 一致で skip し `False` を返す
- x86_64 ホスト上で arm64 sysroot を引くために apt.conf に `APT::Architecture` と `APT::Architectures` を書き、sources.list に `deb [arch=arm64 signed-by=<path>] <url> <suite> <components>` を生成する
- webrtc-build `run.py` の `init_sysroot()` は JSON ロード後に `config.name != target` を検証し、mismatch 時に `RuntimeError` を送出する。sora-cpp-sdk 側の薄いラッパでも同等の検証を行う

`tests/test_sysroot_builder.py` は 11 のユニットテストからなり、`apt-get` / `dpkg-deb` の subprocess を叩かない。`build_sysroot` を叩く 4 ケースは事前配置した manifest / 既存 dir / broken symlink による早期リターンまたは例外検証のみで APT 呼び出しに到達しない。したがって CI で `sudo` 権限やインターネット到達性を要求せず、モックも不要。

JSON スキーマ:

- トップレベル: `name` (ターゲット名、ファイル名の `{target}` と一致必須)、`arch` (APT アーキテクチャ)、`triplet` (`aarch64-linux-gnu` 等)、`packages` (パッケージ配列)、`repositories` (リポジトリ配列)
- `repositories[]`: `url` (HTTPS)、`suite`、`components` (配列)、`signed_by` (GPG キーのパス。絶対パス、または JSON ファイルからの相対パス)
- **packages はトップレベルの単一配列**で、リポジトリ紐付けは持たない。同一パッケージ名が複数リポジトリに存在する場合は APT の依存解決に委ねられる (`libcamera-dev` について「設計方針 1.」で追加対応)
- Raspberry Pi 用鍵は `sysroot/keyrings/raspberrypi-archive-keyring.asc` として同梱し、JSON の `signed_by` から相対パスで参照

**Python バージョン依存**: `sysroot_builder.py` は `from __future__ import annotations` 前提で `tuple[str, ...]` / `dict[str, str] | None` などの PEP 604 記法を使う。関数シグネチャの `X | Y` 記法が評価される箇所で Python 3.10 以上必須。CI ランナー `ubuntu-22.04` / `ubuntu-24.04` の `python3` は既定で 3.10 以上のため実行できる。

**`sysroot_config_fingerprint` とランナー画像更新**: `signed_by` の絶対パス (`/usr/share/keyrings/ubuntu-archive-keyring.gpg` / `/usr/share/keyrings/debian-archive-keyring.gpg`) は CI ランナー画像に含まれる keyring パッケージのバージョンに依存する。ランナー画像更新のたびに fingerprint がずれるため、GitHub Actions キャッシュ (`actions/cache`) を導入しても頻繁に無効化される。本 issue ではキャッシュ導入は行わない。

## 設計方針

### 1. 設定ファイルの置き換え

- 本体用: `multistrap/` を廃止し、リポジトリルート直下の `sysroot/` に JSON 3 ファイル + `keyrings/` を配置
- サンプル用: `examples/multistrap/` を廃止し、`examples/sysroot/` に JSON 3 ファイル + `keyrings/` を配置
- ファイル名はいずれも `{target}.json`
- JSON スキーマは webrtc-build (`sysroot_builder.py` の `load_sysroot_config` が要求する形) をそのまま採用
- **リポジトリ URL は HTTPS に統一**し、`signed_by` による GPG 検証を有効にする

**本体用 `sysroot/ubuntu-22.04_armv8.json`**:

```json
{
    "name": "ubuntu-22.04_armv8",
    "arch": "arm64",
    "triplet": "aarch64-linux-gnu",
    "packages": ["libstdc++-10-dev", "libc6-dev", "libxext-dev", "libdbus-1-dev"],
    "repositories": [
        {
            "url": "https://ports.ubuntu.com/ubuntu-ports",
            "suite": "jammy",
            "components": ["main", "universe"],
            "signed_by": "/usr/share/keyrings/ubuntu-archive-keyring.gpg"
        }
    ]
}
```

`libstdc++-10-dev` は Ubuntu 22.04 (jammy) の `universe` 依存 (`main` に無い) のため `components` に `universe` が必須。既存 multistrap 版との互換維持のため `-10-dev` に固定する。

**本体用 `sysroot/ubuntu-24.04_armv8.json`**:

```json
{
    "name": "ubuntu-24.04_armv8",
    "arch": "arm64",
    "triplet": "aarch64-linux-gnu",
    "packages": ["libstdc++-13-dev", "libc6-dev", "libxext-dev", "libdbus-1-dev"],
    "repositories": [
        {
            "url": "https://ports.ubuntu.com/ubuntu-ports",
            "suite": "noble",
            "components": ["main", "universe"],
            "signed_by": "/usr/share/keyrings/ubuntu-archive-keyring.gpg"
        }
    ]
}
```

**本体用 `sysroot/raspberry-pi-os_armv8.json`**:

```json
{
    "name": "raspberry-pi-os_armv8",
    "arch": "arm64",
    "triplet": "aarch64-linux-gnu",
    "packages": [
        "libc6-dev",
        "libstdc++-14-dev",
        "libasound2-dev",
        "libpulse-dev",
        "libudev-dev",
        "libexpat1-dev",
        "libnss3-dev",
        "libxext-dev",
        "libxtst-dev",
        "libcamera-dev"
    ],
    "repositories": [
        {
            "url": "https://deb.debian.org/debian",
            "suite": "trixie",
            "components": ["main"],
            "signed_by": "/usr/share/keyrings/debian-archive-keyring.gpg"
        },
        {
            "url": "https://archive.raspberrypi.com/debian",
            "suite": "trixie",
            "components": ["main"],
            "signed_by": "keyrings/raspberrypi-archive-keyring.asc"
        }
    ]
}
```

`sysroot_builder.py` は上記から次のような `sources.list` を生成する (arch と signed-by が同じ `deb` 行に含まれる):

```
deb [arch=arm64 signed-by=/usr/share/keyrings/debian-archive-keyring.gpg] https://deb.debian.org/debian trixie main
deb [arch=arm64 signed-by=<abs>/sysroot/keyrings/raspberrypi-archive-keyring.asc] https://archive.raspberrypi.com/debian trixie main
```

サンプル用 `examples/sysroot/*.json` は本体用と以下の点だけが異なる:

- **`examples/sysroot/ubuntu-22.04_armv8.json`**: `packages` を `["libstdc++-11-dev", "libc6-dev", "libxext-dev", "libdbus-1-dev", "libudev-dev", "libgles-dev"]` とする
- **`examples/sysroot/ubuntu-24.04_armv8.json`**: `packages` を `["libstdc++-13-dev", "libc6-dev", "libxext-dev", "libdbus-1-dev", "libudev-dev", "libgles-dev"]` とする
- **`examples/sysroot/raspberry-pi-os_armv8.json`**: 本体用と完全に同一

**`libcamera-dev` の取得元制御**: `libcamera-dev` は Debian trixie の `main` にも Raspberry Pi trixie の `main` にも存在し、実行時に必要なのは Raspberry Pi Foundation 版 (soname に `+rpt` suffix が付く)。

- **一次対応 (実測)**: 実装フェーズで `raspberry-pi-os_armv8` の sysroot 生成後 (`_install/raspberry-pi-os_armv8/rootfs/` に生成される) に以下を実行し、`+rpt` を含む soname が取れていることを確認する:

    ```
    find _install/raspberry-pi-os_armv8/rootfs/usr/lib/aarch64-linux-gnu/ -name 'libcamera.so.*' -printf '%f\n'
    ```

    - Raspberry Pi 版: soname 文字列に `+rpt` を含む → OK
    - Debian 版: `+rpt` を含まない → 期待外、二次対応へ進む

    確認結果を実装 PR 本文の「libcamera-dev soname 確認」節に、コマンド出力そのままを貼り付ける

- **二次対応 (pin、条件付き)**: 一次対応で Debian 版が取得された場合のみ実施。以下 5 点を `sysroot_builder.py` に加える (この時点で参照コミットからの fork 状態になる):
  1. `RepositoryConfig` dataclass に `pin_priority: int | None = None` フィールドを追加
  2. `_load_repository()` に `pin_priority` の validation を追加
  3. `build_sysroot()` の APT 隔離環境に `_apt/etc/apt/preferences.d/pin-<suite>.pref` を生成する処理を追加
  4. **同時に `_apt_options()` の `Dir::Etc::preferences=/dev/null` と `Dir::Etc::preferencesparts=/dev/null` を、それぞれ `Dir::Etc::preferences=<work_dir>/preferences` (空ファイルで OK) と `Dir::Etc::preferencesparts=<work_dir>/preferences.d` に差し替える** (元の `/dev/null` のままでは apt が preferences を読まないため pin が沈黙する。除去だけだとホストの `/etc/apt/preferences` を読み込む副作用が出るため、隔離環境向けの実パスに書き換える必要がある)
  5. `sysroot_config_fingerprint()` に `pin_priority` を含める。`tests/test_sysroot_builder.py` に pin テストケースを追加
  そのうえで `sysroot/raspberry-pi-os_armv8.json` の Raspberry Pi リポジトリに `"pin_priority": 990` を書き足す

- 本 issue のスコープは一次対応の実施と、Debian 版が取得された場合の二次対応まで。webrtc-build 上流への還流は二次対応が発火した場合のみ別 issue を open で起票する

**Raspberry Pi 鍵の配置**: `sysroot/keyrings/raspberrypi-archive-keyring.asc` を実体として配置し、`examples/sysroot/keyrings/raspberrypi-archive-keyring.asc` は `../../../sysroot/keyrings/raspberrypi-archive-keyring.asc` への相対 symlink とする。sora-cpp-sdk の CI 実行環境 (Linux) と主要開発対象 (Linux) では `core.symlinks=true` が既定のため問題ない。Windows で ARM64 クロスビルドは行わないため、Windows 上のチェックアウトで symlink がテキスト化されても sysroot 生成には影響しない。

### 2. buildbase.py の修正

**移植するファイル (新規追加)**:

- `sysroot_builder.py` (リポジトリルート、参照コミットから丸ごとコピー)
- `tests/test_sysroot_builder.py` (参照コミットから丸ごとコピー)

移植は `curl` で参照コミットの生ファイルを取得する:

```
curl -sSL https://raw.githubusercontent.com/shiguredo-webrtc-build/webrtc-build/2c15196/sysroot_builder.py -o sysroot_builder.py
mkdir -p tests
curl -sSL https://raw.githubusercontent.com/shiguredo-webrtc-build/webrtc-build/2c15196/tests/test_sysroot_builder.py -o tests/test_sysroot_builder.py
```

GitHub の raw URL は content-addressed (commit hash 指定) のため取得内容の一意性は担保される。`webrtc-build` と `sora-cpp-sdk` は同一 shiguredo プロジェクトで共に Apache-2.0 ライセンス、リポジトリに NOTICE ファイルが無いためクレジット表示や NOTICE 追記は不要。ルート `pyproject.toml` は新設しない (既存 `e2e-test/pyproject.toml` との名前空間衝突を避けるため)。テスト実行は「テスト戦略」で規定する `uv run --with pytest --with pytest-timeout python -m pytest ...` (`python -m pytest` の形式で、cwd をリポジトリルートにして sys.path に `sysroot_builder.py` を含める) に集約する。

**`ruff.toml` 整合**: sora-cpp-sdk と webrtc-build の `ruff.toml` はいずれも `line-length=100` で同一のため、参照コミットの `sysroot_builder.py` は sora-cpp-sdk 側の `ruff.toml` に対してもそのまま lint pass する。ローカルで `uv run ruff check sysroot_builder.py tests/test_sysroot_builder.py` を実行し pass することを確認する。もし将来 sora-cpp-sdk 側の `ruff.toml` が変わって fail した場合は、修正を上流 (webrtc-build) 側にも同時に反映することでドリフトを避ける (fork 化を避ける)。

**buildbase.py の変更**:

- `from pathlib import Path` (未 import なら追加) と `from sysroot_builder import build_sysroot, load_sysroot_config` を import に追加
- `install_rootfs()` (`buildbase.py:1114-1159`、Jetson 用 `libnvbuf_fdmap.so` symlink 補正含む) を削除
- 薄いラッパ `install_sysroot(config_path, install_dir)` を新設。処理内容:
  1. `rootfs_dir = os.path.join(install_dir, "rootfs")`
  2. 旧 multistrap 版 `rootfs/` が manifest `.webrtc-build-sysroot.json` なしで残存している場合、`rm_rf(rootfs_dir)` と `os.remove(os.path.join(install_dir, "rootfs.version"))` (存在すれば) を実行。**削除対象は当該 `install_dir` 配下の `rootfs` と `rootfs.version` のみ**。`boost.version` / `sora.version` / `webrtc.version` などの他 `*.version` は温存
  3. `config = load_sysroot_config(Path(config_path))`
  4. `expected_name = Path(config_path).stem`。`config.name != expected_name` なら `RuntimeError(f"Sysroot config name does not match: expected={expected_name}, actual={config.name}")` を送出
  5. `build_sysroot(config, Path(rootfs_dir))` を呼ぶ (`force=` は渡さない)
- キャッシュ判定は `sysroot_builder` 側の manifest fingerprint に任せるため、`@versioned` デコレータは付けない

強制再構築用の CLI オプションは追加しない。緊急復旧は `rm -rf _install/<target>/rootfs _install/<target>/rootfs.version` の手動オペレーションで対応する。

webrtc-build の `run.py sysroot <target>` (build を伴わずに sysroot だけ生成) は sora-cpp-sdk 側に追加しない。sora-cpp-sdk では `build` サブコマンドの前段で必要に応じて自動生成する既存挙動を維持する。webrtc-build 側の `SYSROOT_CONFIGS` dict も引き写さず、`config_path` は `os.path.join(BASE_DIR, "sysroot", f"{package_name}.json")` で直接組み立てる。

### 3. run.py の修正

本体 `run.py`:

- import 文 (`run.py:40`) の `install_rootfs` を `install_sysroot` に置き換える
- `install_deps()` の `multistrap` 呼び出しブロック (`run.py:222-239`) を以下に置き換える。**旧 `install_rootfs_args` dict の廃止 と、`hashlib.md5` / `rootfs.version` パス生成の削除を伴う**:

```python
# apt-get + dpkg-deb を使った sysroot の構築
if platform.target.package_name in (
    "ubuntu-22.04_armv8",
    "ubuntu-24.04_armv8",
    "raspberry-pi-os_armv8",
):
    config_path = os.path.join(BASE_DIR, "sysroot", f"{platform.target.package_name}.json")
    install_sysroot(config_path=config_path, install_dir=install_dir)
```

Jetson 2 種 (`ubuntu-20.04_armv8_jetson`、`ubuntu-22.04_armv8_jetson`) は分岐対象から削除する。本体 `run.py` の `import hashlib` は `hashlib.sha256` が他所 (`run.py:1256` 付近) で使用中のため **残す**。examples 3 本の `hashlib` は multistrap 分岐でのみ使われているため import ごと撤去可能。

**`sysroot_builder` の import 経路**: 本体 `run.py` はリポジトリルート直下で動作するため、追加の `sys.path` 操作なしで `buildbase` および (推移的に) `sysroot_builder` を import できる。examples 側は既に `examples/*/run.py:9-11` で `BUILDBASE_DIR = os.path.join(BASE_DIR, "..")` (リポジトリルート) を `sys.path.insert(0, ...)` しており、既存パスに `sysroot_builder.py` が含まれるため追加操作不要。

examples 配下の 3 つの `run.py` (`sumomo`、`sdl_sample`、`messaging_recvonly_sample`):

- 本体と同様に multistrap 呼び出しを `install_sysroot()` に置き換え、`install_rootfs_args` dict と `hashlib.md5` / `rootfs.version` 生成を廃止する
- `config_path` は `os.path.join(BASE_DIR, "sysroot", f"{platform}.json")` (実体は `examples/sysroot/` を参照)
- import 文の `install_rootfs` を `install_sysroot` に置き換える (`examples/{sumomo,sdl_sample}/run.py:29`、`examples/messaging_recvonly_sample/run.py:29`)

### 4. CI の修正

`multistrap` パッケージのインストールと sed パッチを削除する。`binutils-aarch64-linux-gnu` (クロスリンカ) と `libgl-dev` (ホスト側の OpenGL 開発ヘッダ) は残す。

sysroot ビルドが要求するホスト側パッケージ:

- `apt-get`、`dpkg-deb`、`ca-certificates`、`ubuntu-keyring` (Ubuntu ランナー標準搭載)
- **`debian-archive-keyring`** (Debian trixie の GPG 検証、**Ubuntu ランナーには既定で入らないため明示的に install する**)

以下の 3 箇所を修正する:

1. `.github/workflows/ci.yml` armv8 ビルドジョブ (`ci.yml:260-262`): install 行を `sudo apt-get -y install binutils-aarch64-linux-gnu libgl-dev debian-archive-keyring` に変更、直後の sed パッチ 2 行を削除
2. `.github/workflows/release.yml` build-ubuntu ジョブ (`release.yml:249-251`): 同上
3. `.github/workflows/release.yml` build-ubuntu-examples ジョブ (`release.yml:660-662`): install 行を `sudo apt-get install -y binutils-aarch64-linux-gnu debian-archive-keyring` に変更、直後の sed パッチ 2 行を削除

`release.yml` build-ubuntu-examples の matrix (`release.yml:629-639`) には ARM ターゲットとして `ubuntu-24.04_armv8` と `raspberry-pi-os_armv8` のみが含まれ `ubuntu-22.04_armv8` は無いが、当該ジョブの条件式 (`ubuntu-24.04_armv8 || raspberry-pi-os_armv8`) をそのまま対象とするため matrix 変更は不要。

**`sysroot-builder-test` ジョブの追加**: `ci.yml` に新規ジョブを追加する。ジョブ名は既存の `build-ubuntu` / `build-macos` / `build-windows` と揃えて `sysroot-builder-test` (名詞ハイフン形) とする。

```yaml
sysroot-builder-test:
  runs-on: ubuntu-24.04
  steps:
    - uses: actions/checkout@v4
    - uses: astral-sh/setup-uv@v6
    - run: uv run --with pytest --with pytest-timeout python -m pytest tests/test_sysroot_builder.py -v -s --timeout=60
```

`python -m pytest` の形式を使う理由: `pytest tests/...` 形式では pytest の rootpath 判定でリポジトリルートが sys.path に載らず、`ModuleNotFoundError: No module named 'sysroot_builder'` で collection 段階から失敗する (参照コミット環境で実測確認済み)。`python -m pytest` にすると cwd (リポジトリルート) が自動的に sys.path に載り、`sysroot_builder` が import できる。`pyproject.toml` / `conftest.py` / `tests/__init__.py` の追加は不要。

`ci.yml` の `build-ubuntu` matrix ジョブと `release.yml` の対応する armv8 系 matrix ジョブに `needs: [sysroot-builder-test]` を追加する。x86_64 / android 系 matrix セルも同じ `needs` によりブロックされるが、`sysroot_builder.py` の共有物として意図的にジョブ全体でブロックする。

### 5. CHANGES.md の更新

`CHANGES.md` の `## develop` 内 `### misc` サブセクションに以下を追記する。`[CHANGE]` を選ぶ根拠は「CI ホスト側の前提パッケージが変わる (`multistrap` install が不要になる) 下位互換なしの変更」。SDK バイナリ ABI には影響しない。担当者行は 2 スペースインデント (`- @voluntas`) で、実装者が異なる場合は実装者の GitHub handle に置換する。

```
- [CHANGE] multistrap を廃止し apt-get + dpkg-deb による sysroot 構築に置き換える
  - `multistrap/` および `examples/multistrap/` を廃止し、`sysroot/` および `examples/sysroot/` に JSON 設定ファイルを配置する
  - リポジトリを HTTPS に統一し、GPG `signed_by` による認証済み取得に切り替える
  - `buildbase.py` の `install_rootfs()` を廃止し、`sysroot_builder.py` (webrtc-build から移植) を経由する `install_sysroot()` に置き換える
  - `run.py` の `install_deps()` から Jetson 系ターゲット (`ubuntu-20.04_armv8_jetson` / `ubuntu-22.04_armv8_jetson`) の到達不能な dead code 分岐を削除する
  - `install_rootfs()` に含まれていた Jetson 用 `libnvbuf_fdmap.so` symlink 補正を削除する
  - CI ランナーへの `multistrap` および sed パッチのインストールを削除し、`debian-archive-keyring` の明示 install を追加する
  - @voluntas
```

## テスト戦略

### ユニットテスト実行環境

`sysroot_builder.py` のテストは以下のコマンドで実行する:

```
uv run --with pytest --with pytest-timeout python -m pytest tests/test_sysroot_builder.py -v -s --timeout=60
```

CI では `sysroot-builder-test` ジョブが同一コマンドを走らせる。`python -m pytest` を使う理由は「設計方針 4.」参照。timeout 60 秒の根拠は、参照コミットのテスト (`def test_` は 11 個、parametrize 展開後は 15 テスト) が `apt-get` / `dpkg-deb` を叩かないユニットテストのみで実測 30 秒未満で完了することによる (実装フェーズでローカル実測して確定する)。

### ビルド確認

- `python3 run.py build <target>` が全 ARM64 ターゲット (`ubuntu-22.04_armv8`、`ubuntu-24.04_armv8`、`raspberry-pi-os_armv8`) で成功する
- 各サンプル × ARM64 ターゲットのビルド:
  - `sumomo`: 3 ターゲット全部で確認
  - `sdl_sample`: `ubuntu-24.04_armv8` で確認 (`libgles-dev` の実効性確認のため)
  - `messaging_recvonly_sample`: `ubuntu-24.04_armv8` で確認

### 新旧 rootfs の同等性確認 (初回のみ、multistrap 削除前に実施)

新旧は同じ `_install/<target>/rootfs/` に生成されるため、**別 worktree** で旧方式を走らせて退避する。実装順序ステップ 9 (`multistrap` 削除の直前) で以下を実施:

```
# 事前準備: 別 worktree で develop HEAD を checkout し、旧方式で生成
git worktree add /tmp/sora-cpp-sdk-multistrap develop
cd /tmp/sora-cpp-sdk-multistrap
python3 run.py build <target>  # 旧 install_rootfs が動作
mv _install/<target>/rootfs _install/<target>/rootfs.multistrap
cd -  # 本ブランチ側へ戻る

# 本ブランチで新方式で生成
python3 run.py build <target>  # 新 install_sysroot が動作

# 差分比較 (本ブランチ側で実行)
diff <(cd /tmp/sora-cpp-sdk-multistrap/_install/<target>/rootfs.multistrap && find . -type f | sort) \
     <(cd _install/<target>/rootfs && find . -type f | grep -v '\./\.webrtc-build-sysroot\.json$' | sort)
diff <(cd /tmp/sora-cpp-sdk-multistrap/_install/<target>/rootfs.multistrap && find . -type l -printf '%P -> %l\n' | sort) \
     <(cd _install/<target>/rootfs && find . -type l -printf '%P -> %l\n' | sort)

# 後始末
git worktree remove /tmp/sora-cpp-sdk-multistrap
```

**判定基準** (新旧いずれか一方にしか存在しないファイルを次のカテゴリに分類する):

- **許容**: 新旧いずれにも `.h` / `.hpp` と `-dev` パッケージ由来の `.so` シンボリックリンクが揃っており、`libudev-dev` / `libgles-dev` / `libcamera-dev` (該当ターゲットのみ) のヘッダとリンクも両方に含まれる。Recommends 由来の追加ファイルが旧側にだけ現れるのは許容 (実行時サポート系でリンク時には使わない)
- **不許容 (追加調査が必要)**: 新側にしか存在しない ELF 実行ファイル、`.so` 実体ファイル、setuid/setgid ビットが立ったファイル。あるいは旧側にしか存在しない `.h` / `.hpp` / `-dev` 由来 `.so` symlink
- **意見が分かれる差分**: 上記に該当しない差分があれば PR 本文の「rootfs 同等性確認」節に列挙し、実装者がどのカテゴリに該当するかと根拠を書く

比較結果 (差分の抜粋、または「差分なし」の記載) を PR 本文の「rootfs 同等性確認」節に貼り付ける。

### エラーパスの確認

薄いラッパ (`install_sysroot()`) が新規に導入する分岐を確認する:

- 既存 multistrap 産 `rootfs/` 残置: 薄いラッパが manifest 不在を検知して `rm_rf` することを確認 (`rootfs` と `rootfs.version` のみ削除、他 `*.version` は温存)
- `config.name != expected_name`: JSON の `name` を故意に別ターゲット名に書き換えて `RuntimeError` が送出されることを確認

## 完了条件

### 実装順序

コミットタイトルは `shiguredo-git` に従い `0002 <日本語命令形の要約>` の形式で書く。以下の順序で 1 ステップずつ commit する。**各コミット単独で HEAD がビルド可能な状態を保つ** ため、`install_sysroot()` の追加と `install_rootfs()` の削除は間に呼び出し側切り替えを挟む 3 段構成にする。**本 PR のコミットはすべてまとめて push し、CI は最終状態のみを検証する前提とする** (中間 commit で `sysroot-builder-test` が単独で走ることは想定しない)。

1. `sysroot_builder.py` と `tests/test_sysroot_builder.py` を参照コミットから `curl` で取得する。同時に `sysroot-builder-test` ジョブを `ci.yml` に追加する (このステップでは既存 `build-ubuntu` matrix への `needs` は張らない。needs 追加はステップ 10 で他の CI 変更と一緒に行う)
2. `sysroot/*.json` (3 本) + `sysroot/keyrings/raspberrypi-archive-keyring.asc` を追加
3. `examples/sysroot/*.json` (3 本) + `examples/sysroot/keyrings/raspberrypi-archive-keyring.asc` を `../../../sysroot/keyrings/raspberrypi-archive-keyring.asc` への相対 symlink として追加
4. `buildbase.py` に `install_sysroot()` **のみ追加** (旧 `install_rootfs()` はまだ残す)。この時点で HEAD は旧方式で引き続きビルド可能
5. `run.py` (本体) の `install_deps()` 内 multistrap 分岐を `install_sysroot()` 呼び出しに置き換え、Jetson 2 種を分岐対象から削除、`install_rootfs_args` dict を撤去、`hashlib.md5` / `rootfs.version` パス生成を撤去。import の `install_rootfs` を `install_sysroot` に差し替える。`import hashlib` は他所使用中のため残す
6. `examples/{sumomo,sdl_sample,messaging_recvonly_sample}/run.py` の multistrap 分岐を 1 コミットにまとめて置き換える。各ファイルで `install_rootfs_args` dict、`hashlib.md5`、`rootfs.version` パス生成を撤去し、`import hashlib` も撤去する (他所使用なし)。import の `install_rootfs` を `install_sysroot` に差し替える
7. `buildbase.py` から `install_rootfs()` を削除する (Jetson 用 `libnvbuf_fdmap.so` symlink 補正含む)。この時点で呼び出し側は既に新方式に切り替わっているため HEAD ビルドは崩れない
8. `libcamera-dev` の一次対応 (commit を伴わない検証工程): `raspberry-pi-os_armv8` の sysroot を生成し、soname に `+rpt` が含まれることを確認。含まれる場合はステップ 8a-8b をスキップ

    - 8a. (二次対応、条件付き): 一次対応で Debian 版が取得された場合、`sysroot_builder.py` に `pin_priority` を含む 5 点の変更を加える (「設計方針 1.」二次対応節参照)
    - 8b. (二次対応、条件付き): `sysroot/raspberry-pi-os_armv8.json` の Raspberry Pi リポジトリに `"pin_priority": 990` を追加

9. **新旧 rootfs 同等性確認** (commit を伴わない検証工程): 「テスト戦略」の「新旧 rootfs の同等性確認」節の手順を実施し、判定基準に照らして許容範囲であることを確認する
10. `multistrap/` と `examples/multistrap/` を削除
11. CI (`ci.yml` / `release.yml`) の multistrap install と sed パッチを削除し、`debian-archive-keyring` を追加、`ci.yml` の `build-ubuntu` matrix ジョブと `release.yml` の対応 armv8 系 matrix ジョブに `needs: [sysroot-builder-test]` を追加
12. `CHANGES.md` の `## develop` 内 `### misc` にエントリを追記

**PR マージ後の後始末** (maintainer が実施、PR 内には含めない): sora-cpp-sdk の main / develop 上で参照コミットにタグを打ち push する。

```
git fetch git@github.com:shiguredo-webrtc-build/webrtc-build.git 2c15196
git tag -a git-vendor/webrtc-build-2c15196 2c15196 -m "vendored source of sysroot_builder.py"
git push origin git-vendor/webrtc-build-2c15196
```

### 成果物

- `multistrap/` と `examples/multistrap/` が削除されている
- `sysroot/` に本体用 JSON 3 ファイル + `keyrings/raspberrypi-archive-keyring.asc` (実体) が配置されている
- `examples/sysroot/` にサンプル用 JSON 3 ファイル + `keyrings/raspberrypi-archive-keyring.asc` (`../../../sysroot/keyrings/raspberrypi-archive-keyring.asc` への相対 symlink) が配置されている
- `sysroot_builder.py` および `tests/test_sysroot_builder.py` が参照コミットから移植されている
- `buildbase.py` に `install_sysroot()` が実装され、`install_rootfs()` が削除されている
- `run.py` および 3 つの examples `run.py` が `install_sysroot()` を使用するように修正され、`install_rootfs_args` dict と `hashlib.md5` / `rootfs.version` 生成が撤去されている
- CI から `multistrap` install と sed パッチが削除され、`debian-archive-keyring` の明示 install、`sysroot-builder-test` ジョブ、および `ci.yml` / `release.yml` の armv8 系 matrix ジョブへの `needs: [sysroot-builder-test]` が追加されている
- `CHANGES.md` の `## develop` 内 `### misc` にサブ箇条書き付き `[CHANGE]` エントリが追記されている

### 動作確認

- `uv run --with pytest --with pytest-timeout pytest tests/test_sysroot_builder.py -v -s --timeout=60` がローカル・CI 双方でパスする
- `python3 run.py build ubuntu-22.04_armv8` / `ubuntu-24.04_armv8` / `raspberry-pi-os_armv8` が成功する
- `python3 examples/sumomo/run.py build <ARM64 ターゲット>` が 3 ターゲット全部で成功する
- `python3 examples/sdl_sample/run.py build ubuntu-24.04_armv8` が成功する
- `python3 examples/messaging_recvonly_sample/run.py build ubuntu-24.04_armv8` が成功する
- 新旧 rootfs の同等性確認 (「テスト戦略」参照) が実施され、PR 本文に diff の抜粋または「差分なし」が記載されている
- 一次対応の `find` コマンド出力が PR 本文の「libcamera-dev soname 確認」節に貼り付けられ、`+rpt` を含む soname が確認されている (含まれない場合は二次対応の実施内容も PR に反映されている)
