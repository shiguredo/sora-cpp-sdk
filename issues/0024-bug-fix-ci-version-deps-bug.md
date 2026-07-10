# CI が VERSION ファイルから CUDA_VERSION を読み取ろうとするバグ

- Priority: High
- Created: 2026-07-10
- Completed: {YYYY-MM-DD}
- Model: DeepSeek V4 Pro
- Branch: feature/fix-ci-version-deps-bug
- Polished: {YYYY-MM-DD}

## 目的

`.github/workflows/ci.yml` と `.github/workflows/release.yml` で CUDA バージョンの取得に `Get-Content "VERSION"` を使用しているが、`VERSION` ファイルはバージョン文字列 1 行のみで `CUDA_VERSION` キーは存在しない。正しくは `Get-Content "DEPS"` を読み取る必要がある。

## 優先度根拠

CI パイプラインで CUDA キャッシュキーが空になり、キャッシュが機能しない。ビルド時間の増加とキャッシュ破損のリスク。High。

## 現状

`.github/workflows/ci.yml:42-48`:

```yaml
- name: Read CUDA version
  id: cuda_version
  shell: pwsh
  run: |
    $content = Get-Content "VERSION" -Raw
    # VERSION には CUDA_VERSION キーが存在しない
```

`VERSION` ファイルの内容は `2026.2.0` のような単一行。`CUDA_VERSION` は `DEPS` ファイルの `CUDA_VERSION=12.9.1-1` に定義されている。

## 設計方針

`Get-Content "VERSION"` を `Get-Content "DEPS"` に修正する。

## 完了条件

- CI が正しく `DEPS` から `CUDA_VERSION` を読み取ること
