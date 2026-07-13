# CI が VERSION ファイルから CUDA_VERSION を読み取ろうとするバグ

- Priority: High
- Created: 2026-07-10
- Completed: 2026-07-14
- Model: DeepSeek V4 Pro
- Branch: feature/fix-ci-version-deps-bug
- Polished: 2026-07-10

## 目的

`.github/workflows/ci.yml` と `.github/workflows/release.yml` の Windows ビルドジョブで、CUDA バージョンの取得に `Get-Content "VERSION"` を使用している。`VERSION` ファイルはバージョン文字列 1 行のみ（例: `2026.2.0-canary.20`）で `CUDA_VERSION` キーは存在しない。

2025.5.0 の VERSION/DEPS 分離時に、Linux ビルドの `source DEPS` は修正されたが、Windows ビルドの `Get-Content "VERSION"` が取り残された（`CHANGES.md:347`）。

また、キャッシュヒット時に `cuda.version` ファイルを書き出すステップ（`ci.yml:59`、`release.yml:48`）も `${CUDA_VERSION}` という未定義のシェル変数を参照しており、正しい CUDA バージョンが書き出されていない。こちらも併せて修正する。

## 優先度根拠

CI パイプラインで CUDA キャッシュキーが空文字列になり、キャッシュがヒットしない。毎回約 3GB の CUDA インストールバイナリがダウンロードされ、ビルド時間が無駄に増加する。High。

## 現状

**`.github/workflows/ci.yml:41-49`（Windows ビルド）**:

```yaml
      - name: Get Versions
        run: |
          Get-Content "VERSION" | Foreach-Object {
            if (!$_) { continue }
            $var = $_.Split('=')
            New-Variable -Name $var[0] -Value $var[1] -Force
          }
          echo "cuda_version=${CUDA_VERSION}" >> ${Env:GITHUB_OUTPUT}
        id: versions
```

`VERSION` ファイルの内容は `2026.2.0-canary.20` の単一行で `=` を含まない。`Split('=')` の結果 `$var[1]` は空になり、`CUDA_VERSION` という変数は作成されない。`echo "cuda_version=${CUDA_VERSION}"` の `${CUDA_VERSION}` は空文字列として展開され、`steps.versions.outputs.cuda_version` も空になる。

**`.github/workflows/release.yml:30-38`（Windows ビルド）**:

ci.yml と同一のバグ。

**キャッシュヒット時の `cuda.version` 書き出し**:

`ci.yml:59-60` / `release.yml:48-49` の以下は、`Get Versions` ステップとは別の PowerShell セッションで実行されるため、`${CUDA_VERSION}` は常に空文字列になる:

```yaml
      - run: echo "${CUDA_VERSION}" > _install\windows_x86_64\release\cuda.version
        if: steps.cache-cuda.outputs.cache-hit == 'true'
```

**参考: Linux ビルドは修正済み**

`ci.yml:233-239` / `release.yml:222-228` では正しく `source DEPS` を使用している:

```yaml
      - name: Get versions from DEPS
        ...
        run: |
          source DEPS
          echo "cuda_version=$CUDA_VERSION" >> $GITHUB_OUTPUT
```

`DEPS` ファイルの該当行: `CUDA_VERSION=12.9.1-1`。

## 設計方針

1. `ci.yml` と `release.yml` の Windows ビルド `Get Versions` ステップで `Get-Content "VERSION"` を `Get-Content "DEPS"` に修正する
   - `DEPS` は `KEY=VALUE` 形式で、既存の `Split('=')` + `New-Variable` のロジックがそのまま利用できる
   - `DEPS` には `CUDA_VERSION` 以外にも `WEBRTC_BUILD_VERSION`、`BOOST_VERSION` 等の変数が含まれるが、後続ステップに影響しないため問題ない
   - ステップ名・ID（`Get Versions` / `versions`）は変更せず維持する
2. キャッシュヒット時の `echo "${CUDA_VERSION}"` を `echo "${{ steps.versions.outputs.cuda_version }}"` に修正する
   - 別セッションのシェル変数ではなく、前ステップの `GITHUB_OUTPUT` 経由で正しいバージョン文字列を取得する

## 完了条件

- `.github/workflows/ci.yml:43` の `Get-Content "VERSION"` が `Get-Content "DEPS"` に修正されていること
- `.github/workflows/release.yml:32` の `Get-Content "VERSION"` が `Get-Content "DEPS"` に修正されていること
- `.github/workflows/ci.yml:59` の `echo "${CUDA_VERSION}"` が `echo "${{ steps.versions.outputs.cuda_version }}"` に修正されていること
- `.github/workflows/release.yml:48` の `echo "${CUDA_VERSION}"` が `echo "${{ steps.versions.outputs.cuda_version }}"` に修正されていること
- CI 実行時、Cache CUDA ステップのキャッシュキーに正しい `CUDA_VERSION`（例: `12.9.1-1`）が含まれること
- キャッシュヒット時、`cuda.version` ファイルに正しい CUDA バージョンが書き出されること
- `CHANGES.md` の `## develop` 配下、`### misc` セクションに `[FIX]` エントリを追記する:
  ```
  - [FIX] CI が VERSION ファイルから CUDA_VERSION を読み取ろうとするバグを修正する
    - @<担当者>

## 解決方法

`.github/workflows/ci.yml` と `.github/workflows/release.yml` の Windows ビルドジョブで以下の修正を行った。

- `Get-Content "VERSION"` を `Get-Content "DEPS"` に修正し、`DEPS` から `CUDA_VERSION` を正しく取得するようにした
- キャッシュヒット時の `echo "${CUDA_VERSION}"` を `echo "${{ steps.versions.outputs.cuda_version }}"` に修正し、前ステップの出力から正しいバージョン文字列を取得するようにした
  ```
