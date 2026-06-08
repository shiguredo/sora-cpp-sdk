# multistrap を廃止し apt-get + dpkg-deb による sysroot 構築に置き換える

- Priority: Medium
- Created: 2026-06-08
- Model: DeepSeek V4 Pro
- Branch: feature/change-replace-multistrap-with-sysroot

## 目的

multistrap は Debian unstable から 2025-01-24 に削除され、Ubuntu でも 25.04 (questing) 以降から消えているため、代替手段として `apt-get` + `dpkg-deb` を直接利用した sysroot 構築方式に移行する。

根拠:
- Debian Package Tracker: `[2025-01-24] Removed 2.2.11 from unstable`、`[2025-01-25] multistrap REMOVED from testing`。「package is gone」と表示されている。
  - https://tracker.debian.org/pkg/multistrap
- Ubuntu Packages Search: jammy (22.04), noble (24.04) には multistrap が存在するが questing (25.04), resolute (25.10), stonking (26.04) には存在しない。
  - https://packages.ubuntu.com/search?keywords=multistrap&searchon=names

## 優先度根拠

multistrap は既に Debian unstable および Ubuntu 25.04 以降から削除されている。現在の CI は Ubuntu 24.04 (noble) 上で実行しているため multistrap はまだ利用可能だが、今後 CI を Ubuntu 25.04 以降にアップグレードする際にブロッカーとなる。また 24.04 のサポート終了までに移行を完了する必要がある。ただし現時点で即座にCI が壊れているわけではないため High ではなく Medium とする。

## 現状

3 つの multistrap 設定ファイル (`multistrap/*.conf`) を使って ARM64 クロスコンパイル用 sysroot を構築している:

| 設定ファイル | ターゲット OS | 主なパッケージ |
|---|---|---|
| `multistrap/ubuntu-22.04_armv8.conf` | Ubuntu 22.04 (jammy) | libc6-dev, libstdc++-10-dev, libxext-dev, libdbus-1-dev |
| `multistrap/ubuntu-24.04_armv8.conf` | Ubuntu 24.04 (noble) | libc6-dev, libstdc++-13-dev, libxext-dev, libdbus-1-dev |
| `multistrap/raspberry-pi-os_armv8.conf` | Debian trixie | libc6-dev, libstdc++-14-dev, libasound2-dev, libpulse-dev, libudev-dev, libexpat1-dev, libnss3-dev, libxext-dev, libxtst-dev, libcamera-dev |

`buildbase.py` の `install_rootfs()` 関数 (`buildbase.py:1076`) が `multistrap --no-auth -a arm64 -d <rootfs_dir> -f <conf>` を実行し、展開後の絶対パスシンボリックリンクの相対パス化と Jetson 用シンボリックリンクの作成を行っている。

`run.py` の `install_deps()` (`run.py:239-256`) が conf ファイルの MD5 ハッシュをバージョンとして `install_rootfs()` を呼び出す。同様の処理は各 example の `run.py` にも存在する。

CI (`ci.yml:258-262`, `release.yml:248-255`) では multistrap パッケージのインストールに加え、HTTP リポジトリからの取得を許可するための sed パッチ適用が必要。

## 設計方針

### 1. 設定ファイルの置き換え

- `multistrap/` ディレクトリを廃止し、`sysroot/` ディレクトリに JSON 形式の設定ファイルを作成する
- JSON 設定ファイルには以下を含める:
  - `name`: sysroot 識別名
  - `arch`: APT アーキテクチャ (例: `arm64`)
  - `packages`: インストールするパッケージ一覧
  - `repos`: APT リポジトリ定義 (`url`, `suites`, `components`)

### 2. buildbase.py の修正

`install_rootfs()` を削除し、`install_sysroot()` 関数を新設する。内部動作:

1. APT 隔離環境の作成 (`apt.conf`, `sources.list` の生成)
2. `apt-get update` の実行
3. `apt-get -d -y -o Debug::NoLocking=true install` でパッケージをダウンロードのみ
4. `dpkg-deb -x` で各 `.deb` を展開
5. 絶対パスシンボリックリンクの相対パス化（既存のロジックを維持）
6. usrmerge シンボリックリンクの生成（`bin` → `usr/bin` 等）
7. Jetson 用 `libnvbuf_fdmap.so` シンボリックリンクの作成（既存のロジックを維持）
8. `@versioned` デコレータによるキャッシュ（既存の仕組みを維持）

APT の隔離には以下のオプションを使用し、ホストの APT 設定に影響を与えないようにする:
- `APT_CONFIG` 環境変数で専用の `apt.conf` を指定
- `Dir::State`, `Dir::Cache`, `Dir::Etc::sourcelist` を隔離ディレクトリに向ける
- `Dir::Etc::sourceparts`, `Dir::Etc::preferences`, `Dir::Etc::preferencesparts` を `/dev/null` に向ける

HTTP リポジトリからの取得については `Acquire::AllowInsecureRepositories=true` を `apt.conf` に含めることで対応する（multistrap の sed パッチと同等）。

### 3. run.py の修正

- `install_deps()` 内の multistrap 呼び出し部分 (`run.py:239-256`) を `install_sysroot()` 呼び出しに置き換える
- conf ファイルの MD5 ハッシュによるバージョン管理は JSON ファイルに対して同様に行う
- `sysroot = os.path.join(install_dir, "rootfs")` のパスは変更しない（後続の CMake 設定への影響を最小化）

### 4. example の run.py の修正

以下の 3 ファイルで同様の修正を行う:
- `examples/sumomo/run.py`
- `examples/sdl_sample/run.py`
- `examples/messaging_recvonly_sample/run.py`

### 5. CI の修正

- `multistrap` パッケージのインストールを削除
- multistrap の sed パッチ適用を削除
- `binutils-aarch64-linux-gnu` は引き続き必要（クロスリンカとして使用）
- `libgl-dev` は引き続き必要（OpenGL 開発用）

### 6. CHANGES.md の更新

`[CHANGE]` エントリとして変更履歴に追記する。

## 完了条件

- `multistrap/` ディレクトリが削除され、`sysroot/` ディレクトリに JSON 設定ファイルが作成されている
- `buildbase.py` に `install_sysroot()` が実装され、`install_rootfs()` が削除されている
- `run.py` および各 example の `run.py` が `install_sysroot()` を使用するように修正されている
- CI から multistrap 関連の処理が削除されている
- armv8 クロスコンパイル (`ubuntu-22.04_armv8`, `ubuntu-24.04_armv8`, `raspberry-pi-os_armv8`) が正常に動作する
