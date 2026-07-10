# multistrap を廃止し apt-get + dpkg-deb による sysroot 構築に置き換える

- Priority: Medium
- Created: 2026-06-08
- Polished: 2026-07-10
- Model: DeepSeek V4 Pro
- Branch: feature/change-replace-multistrap-with-sysroot

## 目的

multistrap は Debian unstable から 2025-01-24 に削除され、Ubuntu でも 25.04 (questing) 以降から消えているため、代替手段として `apt-get` + `dpkg-deb` を直接利用した sysroot 構築方式に移行する。

## 優先度根拠

現在の CI は Ubuntu 24.04 (noble) 上で実行しているため multistrap はまだ利用可能だが、今後 CI を Ubuntu 25.04 以降にアップグレードする際にブロッカーとなる。また Ubuntu 24.04 LTS の標準サポート終了 (2029 年 4 月) までに移行を完了する必要がある。ただし現時点で即座に CI が壊れているわけではないため High ではなく Medium とする。

## 現状

### multistrap 設定ファイル

3 つの multistrap 設定ファイル (`multistrap/*.conf`) を使って ARM64 クロスコンパイル用 sysroot を構築している:

| 設定ファイル | ターゲット OS | リポジトリ | セクション | 主なパッケージ |
|---|---|---|---|---|
| `multistrap/ubuntu-22.04_armv8.conf` | Ubuntu 22.04 (jammy) | ports.ubuntu.com (HTTP) | Ports | libc6-dev, libstdc++-10-dev, libxext-dev, libdbus-1-dev |
| `multistrap/ubuntu-24.04_armv8.conf` | Ubuntu 24.04 (noble) | ports.ubuntu.com (HTTP) | Ports | libc6-dev, libstdc++-13-dev, libxext-dev, libdbus-1-dev |
| `multistrap/raspberry-pi-os_armv8.conf` | Debian trixie | deb.debian.org (HTTP) | Deb | libc6-dev, libstdc++-14-dev, libasound2-dev, libpulse-dev, libudev-dev, libexpat1-dev, libnss3-dev, libxext-dev, libxtst-dev |
| (同上、2 セクション目) | Raspberry Pi OS trixie | archive.raspberrypi.org (HTTP) | Rasp | libcamera-dev |

raspberry-pi-os_armv8 の `[Deb]` セクションでは `components` が省略されている（multistrap のデフォルトは `main`）。全パッケージは Debian `main` に含まれるため、JSON では `"components": "main"` とする。

いずれの conf も `--no-auth` コマンドラインオプションで認証をスキップしている（`noauth=true` は ubuntu 系 conf にのみ明示されているが、raspberry-pi-os も `--no-auth` によりグローバルに適用される）。

### buildbase.py の install_rootfs

`buildbase.py` の `install_rootfs()` が `multistrap --no-auth -a arm64 -d <rootfs_dir> -f <conf>` を実行し、以下を行う:
1. `rm_rf(rootfs_dir)` による既存 rootfs のクリーンアップ
2. 絶対パスシンボリックリンクの相対パス化
3. Jetson 用 `libnvbuf_fdmap.so` シンボリックリンクの作成（ファイルが存在する場合のみ、全ターゲットで試行）

### 呼び出し元

`install_rootfs()` は以下の 4 ファイルから呼び出されている:
- `run.py`（本体）
- `examples/sumomo/run.py`
- `examples/sdl_sample/run.py`
- `examples/messaging_recvonly_sample/run.py`

各ファイルの `install_deps()` 内で、ARM64 クロスコンパイルターゲットに対して conf ファイルの MD5 ハッシュをバージョンとして `install_rootfs()` を呼び出す。

### Jetson ターゲット

`run.py` では `install_rootfs()` の呼び出し対象に `ubuntu-20.04_armv8_jetson` と `ubuntu-22.04_armv8_jetson` が含まれているが、対応する multistrap 設定ファイルは存在しない。これらのターゲットは AVAILABLE_TARGETS にも含まれていない。本 issue ではこれらの Jetson ターゲットの条件分岐ごと削除し、`buildbase.py` 内の Jetson 用シンボリックリンクロジックも併せて削除する。

### CI での multistrap 使用箇所

- `ci.yml`: armv8 ビルドジョブ（`multistrap` + `binutils-aarch64-linux-gnu` + `libgl-dev` のインストール、sed パッチ適用）
- `release.yml` ビルドジョブ: 同上
- `release.yml` `build-ubuntu-examples` ジョブ: `multistrap` + `binutils-aarch64-linux-gnu` のインストール、sed パッチ適用（`libgl-dev` は別ステップで既にインストール済み）

いずれも HTTP リポジトリからの取得を許可するために sed パッチで `Acquire::AllowInsecureRepositories=true` を multistrap に注入している。

## 設計方針

### 1. 設定ファイルの置き換え

- `multistrap/` ディレクトリを廃止し、リポジトリルート直下の `sysroot/` ディレクトリに JSON 形式の設定ファイルを 3 つ作成する
- ファイル名は `{target}.json`

**`sysroot/ubuntu-22.04_armv8.json`**:

```json
{
  "arch": "arm64",
  "repos": [
    {
      "url": "http://ports.ubuntu.com",
      "suites": "jammy",
      "components": "main universe",
      "packages": ["libstdc++-10-dev", "libc6-dev", "libxext-dev", "libdbus-1-dev"],
      "insecure": true
    }
  ]
}
```

**`sysroot/ubuntu-24.04_armv8.json`**:

```json
{
  "arch": "arm64",
  "repos": [
    {
      "url": "http://ports.ubuntu.com",
      "suites": "noble",
      "components": "main universe",
      "packages": ["libstdc++-13-dev", "libc6-dev", "libxext-dev", "libdbus-1-dev"],
      "insecure": true
    }
  ]
}
```

**`sysroot/raspberry-pi-os_armv8.json`**:

```json
{
  "arch": "arm64",
  "repos": [
    {
      "url": "http://deb.debian.org/debian",
      "suites": "trixie",
      "components": "main",
      "packages": ["libc6-dev", "libstdc++-14-dev", "libasound2-dev", "libpulse-dev", "libudev-dev", "libexpat1-dev", "libnss3-dev", "libxext-dev", "libxtst-dev"],
      "insecure": true
    },
    {
      "url": "http://archive.raspberrypi.org/debian",
      "suites": "trixie",
      "components": "main",
      "packages": ["libcamera-dev"],
      "insecure": true
    }
  ]
}
```

JSON の各フィールド:
- `arch`: APT アーキテクチャ（デフォルト `"arm64"`）
- `repos`: APT リポジトリ定義の配列。複数リポジトリの場合は配列要素を追加する。同一パッケージを複数リポジトリに指定してはならない
  - `url`: リポジトリのベース URL
  - `suites`: スイート名
  - `components`: コンポーネント（スペース区切り）
  - `packages`: このリポジトリから取得するパッケージ一覧
  - `insecure`: `true` の場合 `Acquire::AllowInsecureRepositories=true` と `APT::Get::AllowUnauthenticated "true"` を apt.conf に設定する。既存の multistrap の `--no-auth` + CI sed パッチと同等の効果を得るため、全リポジトリに `true` を設定する

### 2. buildbase.py の修正

`install_rootfs()` を削除し、`install_sysroot()` 関数を新設する:

```python
@versioned
def install_sysroot(version, install_dir, config_path):
```

- `version`: バージョン文字列（`@versioned` がキャッシュ判定に使用）
- `install_dir`: インストール先のルートディレクトリ
- `config_path`: JSON 設定ファイルのパス（`arch` は JSON から読み取るため、引数からは削除する）

#### 処理ステップ

**1. 初期化とクリーンアップ**

`rootfs_dir = os.path.join(install_dir, "rootfs")` を定義し、`rm_rf(rootfs_dir)` を実行。`_apt` ディレクトリは `os.path.join(install_dir, "_apt")` とし、同様に `rm_rf` でクリーンアップする。

**2. APT 隔離環境の作成**

`_apt` 配下に以下のディレクトリを `mkdir_p` で作成する:
- `_apt/etc/apt/` (sources.list、apt.conf の配置先)
- `_apt/var/lib/apt/lists/` (apt-get update の出力先)
- `_apt/var/cache/apt/archives/` (apt-get install -d の .deb ダウンロード先)
- `_apt/var/lib/dpkg/` (dpkg ステータスファイル用)

空の dpkg ステータスファイル (`_apt/var/lib/dpkg/status`) を `touch` で作成する。

`_apt/etc/apt/apt.conf` を以下の内容で生成する:

```
Dir "{_apt}";
APT::Architecture "{arch}";
```

`{_apt}` は `_apt` ディレクトリの絶対パス、`{arch}` は JSON の `arch` 値。insecure なリポジトリが 1 つでもあれば以下を追記する:

```
Acquire::AllowInsecureRepositories "true";
APT::Get::AllowUnauthenticated "true";
```

`_apt/etc/apt/sources.list` を生成する。各行のフォーマット:

```
deb [arch={arch}] {url} {suites} {components}
```

`trusted=yes` は apt.conf 側で制御するため sources.list には含めない。

**3. `apt-get update` の実行**

環境変数 `APT_CONFIG` に `_apt/etc/apt/apt.conf` の絶対パスを指定して `cmd(["apt-get", "update"])` を実行する。`cmd()` は `check=True` のため、apt-get が非ゼロ終了コードを返した場合は `subprocess.CalledProcessError` が送出され処理が中断される。

**4. パッケージのダウンロード**

全リポジトリの `packages` を結合したパッケージリストに対して、環境変数 `APT_CONFIG` を指定した上で以下を実行する:

```
apt-get -d -y --no-install-recommends install <packages>
```

`--no-install-recommends` で推奨パッケージのダウンロードを抑制し、sysroot のサイズを最小化する。`-d` によりダウンロードのみ行い、展開は行われない。依存解決に失敗した場合は `cmd()` により `CalledProcessError` が送出される。

**5. `.deb` の展開**

`glob.glob(os.path.join(install_dir, "_apt", "var", "cache", "apt", "archives", "*.deb"))` で `.deb` ファイルを収集し、ファイル名で `sorted()` して展開順序を決定的にする。

各 `.deb` に対して `dpkg-deb -x <deb> <rootfs_dir>` を実行する。ファイル競合は Debian ポリシー上発生しない前提とする（実際に競合が発生した場合は別 issue で対応）。

展開に失敗した場合は `rm_rf(rootfs_dir)` でクリーンアップした上で例外を送出し、中途半端な rootfs が残らないようにする。

**6. 絶対パスシンボリックリンクの相対パス化**

既存の `install_rootfs()` のロジックをそのまま流用する。

**7. usrmerge シンボリックリンクの確認**

`dpkg-deb -x` は postinst スクリプトを実行しないが、data.tar 内のシンボリックリンクはそのまま展開される。`-dev` パッケージおよびその依存先ランタイムパッケージの両方について、postinst 非実行はクロスコンパイル用 sysroot に影響しない（multistrap も postinst を実行しないため同等）。

以下のシンボリックリンクが存在しない場合は手動で作成する:
- `{rootfs_dir}/bin` → `usr/bin`
- `{rootfs_dir}/sbin` → `usr/sbin`
- `{rootfs_dir}/lib` → `usr/lib`

Ubuntu 22.04 (jammy) は usrmerge 移行前のため存在しなくても問題ないが、念のため確認のみ行う。

**8. キャッシュ**

`@versioned` デコレータにより管理。version は JSON 設定ファイルの MD5 ハッシュ。`rootfs.version` ファイルが存在しなければ、または version が一致しなければ、関数が実行される。

### 3. run.py の修正

本体 `run.py`:
- `install_deps()` 内の multistrap 呼び出しを `install_sysroot()` に置き換える
- conf ファイルパスは `os.path.join(BASE_DIR, "sysroot", f"{platform.target.package_name}.json")` に変更する（呼び出しキー名も `"conf"` → `"config_path"` に変更）
- version は JSON ファイルの MD5 ハッシュを使用する
- Jetson ターゲット（`ubuntu-20.04_armv8_jetson`、`ubuntu-22.04_armv8_jetson`）は条件分岐から削除する
- `from buildbase import ... install_rootfs ...` を `install_sysroot` に置き換える

examples 配下の 3 つの `run.py`:
- 同様の修正を行う。ただし例の各 `run.py` では `BASE_DIR` が `examples/` を指すため、`config_path` の解決は `os.path.join(BASE_DIR, "..", "sysroot", f"{platform}.json")` とする（リポジトリルートの `sysroot/` を参照するため）
- 各 example には Jetson ターゲットが含まれていないため、ターゲットリストの変更は不要

### 4. CI の修正

以下の 3 箇所を修正する:

1. `ci.yml`: armv8 ビルドジョブの `multistrap` インストール + sed パッチ適用を削除。`binutils-aarch64-linux-gnu` と `libgl-dev` は引き続き必要
2. `release.yml` ビルドジョブ: 同様
3. `release.yml` `build-ubuntu-examples` ジョブ: `multistrap` インストール + sed パッチ適用を削除。`binutils-aarch64-linux-gnu` は引き続き必要（`libgl-dev` は別ステップで既にインストール済み）

### 5. CHANGES.md の更新

`## develop` の `### misc` セクションに `[CHANGE]` エントリを追記する。

## テスト戦略

- **CI でのビルド確認**: `python3 run.py build <target>` が全 ARM64 ターゲットで成功すること（クロスコンパイル環境では `--run-e2e-test` は実行されないため、ビルドの成否のみを検証する）
- **新旧 rootfs の同等性確認**（手動、初回のみ）: 全 3 ターゲットについて、新旧 rootfs のファイル一覧とシンボリックリンク一覧を比較し、不足がないことを確認する

## 完了条件

- `multistrap/` ディレクトリが削除され、`sysroot/` ディレクトリに 3 つの JSON 設定ファイルが作成されている
- `buildbase.py` に `install_sysroot()` が実装され、`install_rootfs()` と Jetson 用シンボリックリンクロジックが削除されている
- `run.py` の Jetson ターゲットが multistrap 条件分岐から削除されている
- `run.py` および 3 つの example `run.py` が `install_sysroot()` を使用するように修正されている
- CI（`ci.yml`、`release.yml` ビルドジョブ、`release.yml` `build-ubuntu-examples` ジョブ）から multistrap 関連の処理が削除されている
- `python3 run.py build ubuntu-22.04_armv8`、`ubuntu-24.04_armv8`、`raspberry-pi-os_armv8` がエラーなく完了すること
- `CHANGES.md` の `## develop` 内 `### misc` セクションに `- [CHANGE] multistrap を廃止し apt-get + dpkg-deb による sysroot 構築に置き換える` 行と、インデントした著者行（`  - @xxx` 形式）が追記されていること
