# multistrap を廃止し apt-get + dpkg-deb による sysroot 構築に置き換える

- Priority: Medium
- Created: 2026-06-08
- Polished: 2026-07-10
- Model: DeepSeek V4 Pro
- Branch: feature/change-replace-multistrap-with-sysroot

## 目的

multistrap は Debian unstable から 2025-01-24 に削除され、Ubuntu でも 25.04 (plucky) 以降から消えているため、代替手段として `apt-get` + `dpkg-deb` を直接利用した sysroot 構築方式に移行する。

本 issue の対象は本体 (リポジトリルート) とサンプル (`examples/`) の両方の ARM64 クロスコンパイル用 sysroot 構築であり、両者はパッケージ構成が異なる (後述) ため別々の設定ファイル群として扱う。

## 優先度根拠

現在の CI は Ubuntu 24.04 (noble) 上で実行しているため multistrap はまだ利用可能だが、今後 CI を Ubuntu 25.04 以降にアップグレードする際にブロッカーとなる。また Ubuntu 24.04 LTS の標準サポート終了 (2029 年 4 月) までに移行を完了する必要がある。ただし現時点で即座に CI が壊れているわけではないため High ではなく Medium とする。

## 現状

### multistrap 設定ファイル (本体・サンプルで別系統)

multistrap 設定ファイルは 2 系統存在する。本体用の `multistrap/*.conf` (`run.py` が参照) と、サンプル用の `examples/multistrap/*.conf` (`examples/*/run.py` が参照) である。**両者はパッケージ構成が異なる** ため、移行時にこの差異を維持しなければならない。

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

サンプルの `libudev-dev` はゲームパッド等の入力デバイス、`libgles-dev` は SDL3 経由の OpenGL ES 描画のために必要であり、これらが欠落するとサンプルのクロスビルドが破綻する。したがって移行後もサンプル用 sysroot は本体用とは別のパッケージ集合を持たせる。

raspberry-pi-os_armv8 の `[Deb]` / `[Rasp]` セクションでは `components` が省略されている (multistrap のデフォルトは `main`)。全パッケージは Debian / Raspberry Pi OS の `main` に含まれるため、JSON では `"components": "main"` とする。

ubuntu 系 conf の `components=main universe` は multistrap ではスペース区切りである。JSON の `components` も同じくスペース区切りとし、生成する `sources.list` の `deb` 行にそのまま埋め込む。

いずれの conf も `--no-auth` コマンドラインオプションで認証をスキップしている (`noauth=true` は ubuntu 系 conf にのみ明示されているが、raspberry-pi-os も `--no-auth` によりグローバルに適用される)。

各 conf の `bootstrap`(パッケージ取得元) と `aptsources`(sources.list 追加対象) は全ファイルで同一セクションを指しており、依存解決に使うリポジトリとパッケージ取得に使うリポジトリが分離されていない。したがって移行後に全リポジトリを一律に `sources.list` へ追加する方式でも挙動は変わらない。

### buildbase.py の install_rootfs

`buildbase.py` の `install_rootfs()` が `multistrap --no-auth -a arm64 -d <rootfs_dir> -f <conf>` を実行し、以下を行う:
1. `rm_rf(rootfs_dir)` による既存 rootfs のクリーンアップ
2. 絶対パスシンボリックリンクの相対パス化
3. Jetson 用 `libnvbuf_fdmap.so` シンボリックリンクの作成 (ファイルが存在する場合のみ、全ターゲットで試行)

### 呼び出し元

`install_rootfs()` は以下の 4 ファイルから呼び出されている:
- `run.py` (本体、`multistrap/` を参照)
- `examples/sumomo/run.py` (`examples/multistrap/` を参照)
- `examples/sdl_sample/run.py` (`examples/multistrap/` を参照)
- `examples/messaging_recvonly_sample/run.py` (`examples/multistrap/` を参照)

各ファイルの `install_deps()` 内で、ARM64 クロスコンパイルターゲットに対して conf ファイルの MD5 ハッシュをバージョンとして `install_rootfs()` を呼び出す。本体は `os.path.join(BASE_DIR, "multistrap", ...)` (`BASE_DIR` はリポジトリルート)、各サンプルは `os.path.join(BASE_DIR, "multistrap", ...)` (各サンプルの `BASE_DIR` は `examples/` を指すため実体は `examples/multistrap/`) を参照している。この参照先の違いにより、本体とサンプルで別々の conf が使われている。

### Jetson ターゲット

`run.py` の `install_deps()` の条件分岐 (`run.py:223-229` の `if platform.target.package_name in (...)`) では `install_rootfs()` の呼び出し対象に `ubuntu-20.04_armv8_jetson` と `ubuntu-22.04_armv8_jetson` が含まれているが、対応する multistrap 設定ファイルは存在しない。これらのターゲットは `AVAILABLE_TARGETS` (`run.py:668-678`) にも含まれておらず、`argparse` の `choices` で弾かれるため到達不能な dead code である。

`install_sysroot()` に対応する JSON も用意しないため、これらのターゲットを条件分岐に残すと存在しない JSON を参照して失敗する。したがって本 issue では **`install_deps()` の当該条件分岐からこの 2 ターゲットを削除** し、あわせて `install_rootfs()` 内の Jetson 用 `libnvbuf_fdmap.so` シンボリックリンクロジック (`buildbase.py:1141-1157`) を (関数ごと削除するため) 削除する。

**スコープ限定**: 削除するのは上記 2 箇所のみである。`run.py` の他の Jetson 参照 (`run.py:128`, `run.py:376`, `run.py:831`, `run.py:934`, `run.py:1087` の `platform.target.os in ("jetson", ...)` 等) は Jetson プラットフォームサポート全体に関わる別関心事であり、本 issue (multistrap 置き換え) のスコープ外とする。これらの dead code 整理が必要なら別途 refactor issue を起票する。

### CI での multistrap 使用箇所

- `ci.yml`: armv8 ビルドジョブ (`multistrap` + `binutils-aarch64-linux-gnu` + `libgl-dev` のインストール、sed パッチ適用)
- `release.yml` ビルドジョブ: 同上
- `release.yml` `build-ubuntu-examples` ジョブ: `multistrap` + `binutils-aarch64-linux-gnu` のインストール、sed パッチ適用 (`libgl-dev` は別ステップで既にインストール済み)

いずれも HTTP リポジトリからの取得を許可するために sed パッチで `Acquire::AllowInsecureRepositories=true` を multistrap に注入している。

## 設計方針

### 1. 設定ファイルの置き換え

- 本体用: `multistrap/` ディレクトリを廃止し、リポジトリルート直下の `sysroot/` ディレクトリに JSON 形式の設定ファイルを 3 つ作成する
- サンプル用: `examples/multistrap/` ディレクトリを廃止し、`examples/sysroot/` ディレクトリに JSON 形式の設定ファイルを 3 つ作成する
- ファイル名はいずれも `{target}.json`
- 本体とサンプルはパッケージ構成が異なる (現状セクション参照) ため、`sysroot/` へ統合せず別々のディレクトリとして維持する。これにより現状の「本体とサンプルで別 conf」という構造をそのまま踏襲し、サンプル配布物の自己完結性を保つ

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

サンプル用 `examples/sysroot/*.json` は本体用と以下の点だけが異なる (現状セクションの conf 差分を JSON に反映したもの)。それ以外のフィールドは本体用と同一とする:

- **`examples/sysroot/ubuntu-22.04_armv8.json`**: `packages` を `["libstdc++-11-dev", "libc6-dev", "libxext-dev", "libdbus-1-dev", "libudev-dev", "libgles-dev"]` とする (libstdc++ が `-11-dev`、`libudev-dev` と `libgles-dev` を追加)
- **`examples/sysroot/ubuntu-24.04_armv8.json`**: `packages` を `["libstdc++-13-dev", "libc6-dev", "libxext-dev", "libdbus-1-dev", "libudev-dev", "libgles-dev"]` とする (`libudev-dev` と `libgles-dev` を追加)
- **`examples/sysroot/raspberry-pi-os_armv8.json`**: 本体用 `sysroot/raspberry-pi-os_armv8.json` と完全に同一

JSON の各フィールド:
- `arch`: APT アーキテクチャ (デフォルト `"arm64"`)
- `repos`: APT リポジトリ定義の配列。リポジトリが 1 つの場合も要素数 1 の配列で記述する。同一パッケージを複数リポジトリに指定してはならない
  - `url`: リポジトリのベース URL
  - `suites`: スイート名
  - `components`: コンポーネント (スペース区切り)。`sources.list` の `deb` 行にそのまま埋め込む
  - `packages`: このリポジトリから取得するパッケージ一覧
  - `insecure`: `true` の場合 `Acquire::AllowInsecureRepositories "true"` と `APT::Get::AllowUnauthenticated "true"` を apt.conf に設定する。既存の multistrap の `--no-auth` + CI sed パッチと同等の効果を得るため、全リポジトリに `true` を設定する

### 2. buildbase.py の修正

`install_rootfs()` を削除し、`install_sysroot()` 関数を新設する:

```python
@versioned
def install_sysroot(version, install_dir, config_path):
```

- `version`: バージョン文字列 (`@versioned` がキャッシュ判定に使用)
- `install_dir`: インストール先のルートディレクトリ
- `config_path`: JSON 設定ファイルのパス (`arch` は JSON から読み取るため、引数からは削除する)

`@versioned` デコレータ (`buildbase.py:251-270`) は `wrapper(version, version_file, *args, **kwargs)` の形で `version` と `version_file` を消費し、`func` には `version` のみを渡す。したがって `install_sysroot()` のシグネチャに `version_file` は現れないが、**呼び出し側は `version_file` をキーワード引数として渡す必要がある** (これを渡さないと `@versioned` がキャッシュファイルパスを決定できない)。呼び出し例は「3. run.py の修正」を参照。

`buildbase.py` は現状 `json` を import していないため、`import json` を追加する (`glob`、`os`、`shutil`、`subprocess`、`hashlib` は import 済み)。

#### 処理ステップ

**0. JSON 設定の読み込み**

`config_path` の JSON を読み込み、`arch` と全リポジトリを横断したパッケージ一覧を組み立てる。パッケージが 1 つも無い場合は設定不備として例外を送出する:

```python
with open(config_path, encoding="utf-8") as f:
    config = json.load(f)
arch = config.get("arch", "arm64")
repos = config["repos"]
packages = [pkg for repo in repos for pkg in repo["packages"]]
if not packages:
    raise ValueError(f"No packages specified in {config_path}")
insecure = any(repo.get("insecure") for repo in repos)
```

**1. 初期化とクリーンアップ**

`rootfs_dir = os.path.join(install_dir, "rootfs")` を定義し、`rm_rf(rootfs_dir)` を実行。`_apt` ディレクトリは `os.path.join(install_dir, "_apt")` とし、同様に `rm_rf` でクリーンアップする。

**2. APT 隔離環境の作成**

`_apt` 配下に以下のディレクトリを `mkdir_p` で作成する:
- `_apt/etc/apt/` (sources.list、apt.conf の配置先)
- `_apt/etc/apt/sources.list.d/` (空。作成しないと `Dir::Etc::sourceparts` の読み取りで apt が警告を出すため)
- `_apt/var/lib/apt/lists/` (apt-get update の出力先)
- `_apt/var/cache/apt/archives/` (apt-get install -d の .deb ダウンロード先)
- `_apt/var/lib/dpkg/` (dpkg ステータスファイル用)

空の dpkg ステータスファイル (`_apt/var/lib/dpkg/status`) を `touch` で作成する。これにより APT は全パッケージを未インストール状態として依存解決する (multistrap も空 status で同様に動作する)。`-d` (ダウンロードのみ) のため `Essential` パッケージが実際に構成されることはない。

`_apt/etc/apt/apt.conf` を以下の内容で生成する:

```
Dir "{_apt}";
Dir::Bin::methods "/usr/lib/apt/methods";
APT::Architecture "{arch}";
```

`{_apt}` は `_apt` ディレクトリの絶対パス、`{arch}` は JSON の `arch` 値。

`Dir::Bin::methods` の明示は必須である。`Dir "{_apt}"` を設定すると APT のメソッドバイナリ探索パス (`Dir::Bin::methods`) も `{_apt}/usr/lib/apt/methods/` に化けてしまい、`http` 等のメソッドドライバが見つからず `apt-get update` が `E: The method driver ... could not be found.` で失敗する。ホストの標準パス `/usr/lib/apt/methods` を明示して回避する。

insecure なリポジトリが 1 つでもあれば (ステップ 0 の `insecure` が真であれば) 以下を追記する:

```
Acquire::AllowInsecureRepositories "true";
APT::Get::AllowUnauthenticated "true";
```

これはグローバル設定であり、現状は全リポジトリが `insecure: true` のため問題ない。将来 `insecure: false` のリポジトリを混在させる場合は、この方式では secure リポジトリまで認証なしになってしまうため、`sources.list` 行単位の `[trusted=yes]` による制御へ切り替える必要がある。

`_apt/etc/apt/sources.list` を生成する。各行のフォーマット:

```
deb [arch={arch}] {url} {suites} {components}
```

`trusted=yes` は apt.conf 側で制御するため sources.list には含めない。

**3. `apt-get update` の実行**

環境変数 `APT_CONFIG` に `_apt/etc/apt/apt.conf` の絶対パスを指定して `apt-get update` を実行する。`cmd()` (`buildbase.py:65-76`) は `**kwargs` を `subprocess.run()` にそのまま渡すため、`env` で `APT_CONFIG` を注入できる:

```python
apt_conf = os.path.join(install_dir, "_apt", "etc", "apt", "apt.conf")
apt_env = {**os.environ, "APT_CONFIG": apt_conf}
cmd(["apt-get", "update"], env=apt_env)
```

`cmd()` は `check=True` のため、apt-get が非ゼロ終了コードを返した場合は `subprocess.CalledProcessError` が送出され処理が中断される。

**4. パッケージのダウンロード**

ステップ 0 で組み立てた `packages` に対して、同じ `apt_env` を指定して以下を実行する:

```python
cmd(["apt-get", "-d", "-y", "--no-install-recommends", "install", *packages], env=apt_env)
```

`--no-install-recommends` で推奨パッケージのダウンロードを抑制し、sysroot のサイズを最小化する。multistrap も既定で Recommends を展開しないため挙動は同等だが、念のため新旧 rootfs 比較 (テスト戦略) で Recommends 由来のファイル欠落がないことを確認する。`-d` によりダウンロードのみ行い、展開は行われない。依存解決に失敗した場合は `cmd()` により `CalledProcessError` が送出される。

**要検証 (foreign arch のパッケージ取得)**: `APT::Architecture "arm64"` を指定した隔離環境 (空 dpkg status) で、`deb [arch=arm64]` 付き sources.list から arm64 の Packages が取得でき、`apt-get -d install` が arm64 の `.deb` をダウンロードできることを、実装時に Ubuntu 24.04 ホストで実機確認する。もし native arch 上書きだけでは arm64 候補が見えない場合は、apt.conf に `APT::Architectures "arm64";` を追加するか、`dpkg --add-architecture arm64 --admindir=<_apt>/var/lib/dpkg` 相当を手順に追加する。

**5. `.deb` の展開**

`glob.glob(os.path.join(install_dir, "_apt", "var", "cache", "apt", "archives", "*.deb"))` で `.deb` ファイルを収集し、ファイル名で `sorted()` して展開順序を決定的にする (`dpkg-deb -x` は単純なファイル展開のため展開順序は結果に影響しないが、再現性のためソートする)。

収集結果が 0 件の場合は、依存解決やアーキテクチャ設定の不備で空の sysroot が生成されるのを防ぐため例外を送出する (空 rootfs での後続ビルドは不可解なエラーになるため、ここで早期に失敗させる)。

各 `.deb` に対して `dpkg-deb -x <deb> <rootfs_dir>` を実行する。`dpkg-deb` は `dpkg` パッケージに含まれ、CI の Ubuntu ランナーには標準でインストール済みのため追加インストールは不要。

multistrap の `unpack=true` は内部的に `dpkg --unpack` を呼びメンテナスクリプト (preinst) を実行しうるが、対象は arm64 の foreign アーキテクチャであり、`-dev` パッケージおよびその依存先ランタイムパッケージは新規展開時に preinst/postinst で sysroot 内容を変える処理をほぼ持たない。`dpkg-deb -x` は data.tar をそのまま展開する (メンテナスクリプトは実行しない) が、data.tar 内のシンボリックリンクはそのまま展開される。この差が sysroot 内容に影響しないことは、テスト戦略の新旧 rootfs 比較で担保する。

ファイル競合は Debian ポリシー上発生しない前提とする (実際に競合が発生した場合は別 issue で対応)。展開ループ全体を try/except で囲み、いずれかの `.deb` で失敗したら、失敗した `.deb` のファイル名を含む例外を送出する。その前に `rm_rf(rootfs_dir)` で全クリーンアップし、部分展開された中途半端な rootfs を残さない (個別リトライは行わない)。

**6. 絶対パスシンボリックリンクの相対パス化**

既存の `install_rootfs()` の相対パス化ロジック (`buildbase.py:1118-1139`) をそのまま流用する。Jetson 用 `libnvbuf_fdmap.so` シンボリックリンク作成ロジック (`buildbase.py:1141-1157`) は移植しない (Jetson ターゲットは削除するため)。

**7. usrmerge シンボリックリンクの作成**

以下のシンボリックリンクが存在しない場合は作成する (存在する場合は何もしない)。全ターゲットで一律に適用する:
- `{rootfs_dir}/bin` → `usr/bin`
- `{rootfs_dir}/sbin` → `usr/sbin`
- `{rootfs_dir}/lib` → `usr/lib`

これらのトップレベル merged-usr シンボリックリンクは、通常 `base-files` や usrmerge 系パッケージのメンテナスクリプトで整えられる。本方式は選択したパッケージのみを展開し、ベースシステムを含めず、かつ `dpkg-deb -x` がメンテナスクリプトを実行しないため、これらのシンボリックリンクが存在しない可能性がある。存在しなければ作成し、存在すれば何もしない方針で全ターゲットに一律適用する (副作用はない)。

**8. `_apt` の後片付け**

`.deb` 展開が成功した後、`rm_rf(_apt)` で `_apt` ディレクトリ (ダウンロード済み `.deb` や APT メタデータ) を削除し、`install_dir` に不要なキャッシュが残らないようにする。`@versioned` のキャッシュヒット時は `install_sysroot()` 自体がスキップされるため、`_apt` を残しても再利用されない (ディスクを無駄に消費するだけ) ことがこの片付けの根拠である。途中で失敗した場合は `version_file` が書き込まれないため次回に再実行され、その際ステップ 1 の `rm_rf(_apt)` で残留物がクリーンアップされる。

**9. キャッシュ**

`@versioned` デコレータにより管理。version は JSON 設定ファイルの MD5 ハッシュ。`version_file` (本体・サンプルとも `rootfs.version`) の内容と `version` が (`.strip()` 比較で) 一致しなければ、または `version_file` が存在しなければ、関数が実行される。本体とサンプルは `install_dir` が異なる (本体は `<repo>/_install/...`、サンプルは `<repo>/examples/_install/...`) ため、`rootfs.version` の実体パスは別になり衝突しない。

### 3. run.py の修正

本体 `run.py`:
- `install_deps()` の multistrap 呼び出しブロック (223-239 行) を `install_sysroot()` に置き換える
- `# multistrap を使った sysroot の構築` コメント (`run.py:222`) を `# apt-get + dpkg-deb を使った sysroot の構築` 等に更新する
- `config_path` は `os.path.join(BASE_DIR, "sysroot", f"{platform.target.package_name}.json")` とする (`BASE_DIR` はリポジトリルート)
- version は JSON ファイルの MD5 ハッシュを使用する
- 既存の `install_rootfs_args` dict を組み立てて `**` で渡すスタイルを踏襲する。キー名を `"conf"` → `"config_path"` に変更し、`version_file` キー (`os.path.join(install_dir, "rootfs.version")`) は `@versioned` が必要とするため引き続き渡す。書き換え後は以下:

```python
config_path = os.path.join(BASE_DIR, "sysroot", f"{platform.target.package_name}.json")
version_md5 = hashlib.md5(open(config_path, "rb").read()).hexdigest()
install_sysroot_args = {
    "version": version_md5,
    "version_file": os.path.join(install_dir, "rootfs.version"),
    "install_dir": install_dir,
    "config_path": config_path,
}
install_sysroot(**install_sysroot_args)
```

- Jetson ターゲット (`ubuntu-20.04_armv8_jetson`、`ubuntu-22.04_armv8_jetson`) は条件分岐から削除する (現状セクションの「Jetson ターゲット」を参照)
- import 文の `install_rootfs` を `install_sysroot` に置き換える (`run.py:40`)

examples 配下の 3 つの `run.py` (`sumomo`、`sdl_sample`、`messaging_recvonly_sample`):
- 本体と同様に multistrap 呼び出しを `install_sysroot()` に置き換え、`# multistrap を使った sysroot の構築` コメントも更新する
- 各サンプルの `BASE_DIR` は `examples/` を指すため、`config_path` は `os.path.join(BASE_DIR, "sysroot", f"{platform}.json")` とする (実体は `examples/sysroot/` を参照)。**本体の `sysroot/` ではなくサンプル用 `examples/sysroot/` を参照する** 点に注意 (パッケージ構成が異なるため)
- 各サンプルの import 文の `install_rootfs` を `install_sysroot` に置き換える (`examples/*/run.py:29`)
- 各サンプルには Jetson ターゲットが含まれていないため、ターゲットリストの変更は不要

### 4. CI の修正

`multistrap` パッケージのインストールと、multistrap に insecure リポジトリ取得を許可させる sed パッチ (`Acquire::AllowInsecureRepositories=true` 注入) はいずれも不要になるため削除する。`insecure` 相当の設定は `install_sysroot()` が生成する apt.conf 側で行うため、CI での特別な対応は要らない。`binutils-aarch64-linux-gnu` (クロスリンカ) と `libgl-dev` (ホスト側の OpenGL 開発ヘッダ。sysroot には含まれない) は引き続き必要なので残す。

以下の 3 箇所を修正する:

1. `ci.yml` armv8 ビルドジョブ: install 行 (`ci.yml:260`) の `sudo apt-get -y install multistrap binutils-aarch64-linux-gnu libgl-dev` を `sudo apt-get -y install binutils-aarch64-linux-gnu libgl-dev` に変更し、直後の sed パッチ行 (`ci.yml:261-262`) を削除する
2. `release.yml` ビルドジョブ: install 行 (`release.yml:249`) と sed パッチ行 (`release.yml:250-251`) を上記と同様に修正する
3. `release.yml` `build-ubuntu-examples` ジョブ: install 行 (`release.yml:660`) の `sudo apt-get install -y multistrap binutils-aarch64-linux-gnu` を `sudo apt-get install -y binutils-aarch64-linux-gnu` に変更し、直後の sed パッチ行 (`release.yml:661-662`) を削除する (`libgl-dev` はこのジョブでは別ステップ `release.yml:654` で既にインストール済み)

なお `release.yml` の `build-ubuntu-examples` の matrix (`release.yml:628-639`) には ARM ターゲットとして `ubuntu-24.04_armv8` と `raspberry-pi-os_armv8` のみが含まれ `ubuntu-22.04_armv8` は無いが、multistrap 削除は当該ジョブの条件式 (`ubuntu-24.04_armv8 || raspberry-pi-os_armv8`) をそのまま対象とするため matrix 変更は不要。

### 5. CHANGES.md の更新

`## develop` 内の `### misc` サブセクション (`CHANGES.md:138`) に `[CHANGE]` エントリを追記する。`### misc` にはビルド基盤・CI・サンプル系の変更がまとめられており、既存の CI 変更エントリ (例: 「GitHub Actions の Slack 通知を `shiguredo/github-actions` に置き換える」`CHANGES.md:144`) と同じ扱いに揃える。

## テスト戦略

- **CI でのビルド確認**: `python3 run.py build <target>` が全 ARM64 ターゲット (`ubuntu-22.04_armv8`、`ubuntu-24.04_armv8`、`raspberry-pi-os_armv8`) で成功すること。加えて `python3 examples/<sample>/run.py build <target>` が各サンプルの ARM64 ターゲットで成功すること。クロスコンパイル環境では `--run-e2e-test` は実行されないため、ビルドの成否のみを検証する
- **新旧 rootfs の同等性確認** (手動、初回のみ): 本体・サンプルの全ターゲットについて、multistrap で構築した rootfs と `install_sysroot()` で構築した rootfs を比較する。ファイル一覧の差分だけでは内容差を見落とすため、以下を比較対象とする:
  - ファイルパスの一覧 (欠落・過剰の検出)。`find <rootfs> -type f | sort` の差分
  - シンボリックリンクの参照先。`find <rootfs> -type l -printf '%p -> %l\n' | sort` の差分 (`readlink` 結果を含める)
  - `-dev` パッケージがリンクに必要とする `.so` / ヘッダファイルの有無 (Recommends 抑制による欠落検出)
  - 主要パッケージのバージョン (依存解決ロジックの差による版ずれ検出)
  - サンプル用 sysroot については `libudev-dev` / `libgles-dev` 由来のヘッダ・ライブラリが含まれること

  比較は上記の `find` 出力を新旧で `diff` する簡易手順で足りる。専用の比較ツールは作らない (初回の移行検証のみで使い捨てのため)。

## 完了条件

- `multistrap/` ディレクトリと `examples/multistrap/` ディレクトリが削除されている
- `sysroot/` ディレクトリに 3 つの JSON 設定ファイル、`examples/sysroot/` ディレクトリに 3 つの JSON 設定ファイルが作成されている (サンプル用は `libudev-dev` / `libgles-dev` を含み、ubuntu-22.04 は `libstdc++-11-dev` を使う)
- `buildbase.py` に `install_sysroot()` が実装され、`install_rootfs()` (Jetson 用 `libnvbuf_fdmap.so` シンボリックリンクロジックを含む) が削除されている
- `install_sysroot()` が生成する apt.conf に `Dir::Bin::methods "/usr/lib/apt/methods"` が設定されている
- `_apt/etc/apt/sources.list.d/` が作成される
- `.deb` 展開成功後に `_apt` が削除される
- `run.py` の `install_deps()` 内 multistrap 条件分岐から Jetson ターゲット (`ubuntu-20.04_armv8_jetson`、`ubuntu-22.04_armv8_jetson`) が削除されている (`run.py` の他の Jetson 参照はスコープ外)
- `run.py` および 3 つのサンプル `run.py` が `install_sysroot()` を使用するように修正され、それぞれ本体は `sysroot/`、サンプルは `examples/sysroot/` を参照している。import 文も `install_rootfs` → `install_sysroot` に更新され、`# multistrap を使った sysroot の構築` コメントも更新されている
- CI (`ci.yml`、`release.yml` ビルドジョブ、`release.yml` `build-ubuntu-examples` ジョブ) から `multistrap` インストールと sed パッチが削除され、`binutils-aarch64-linux-gnu` / `libgl-dev` は残っている
- `python3 run.py build ubuntu-22.04_armv8`、`ubuntu-24.04_armv8`、`raspberry-pi-os_armv8` がエラーなく完了すること
- `python3 examples/sumomo/run.py build <ARM64 ターゲット>` 等、各サンプルの ARM64 ビルドがエラーなく完了すること
- `CHANGES.md` の `## develop` 内 `### misc` サブセクションに `- [CHANGE] multistrap を廃止し apt-get + dpkg-deb による sysroot 構築に置き換える` 行と、インデントした著者行 (`  - @xxx` 形式) が追記されていること
