# jetpack-6 のビルドを jetson のみにする

- Created: 2026-09-11
- Completed: {YYYY-MM-DD}
- Branch: feature/refactor-jetson-jetpack6-build-only
- Polished: {YYYY-MM-DD}

## 目的

`support/jetson-jetpack-6` ブランチの GitHub Actions ビルドが、Release にアップロードする `ubuntu-22.04_armv8_jetson` 以外のプラットフォーム (windows / macos / ubuntu x86_64 / android) もビルドしており、時間とリソースを浪費している。ビルド対象を jetson のみに絞る。

## 現状

- `support/jetson-jetpack-6` ブランチの `.github/workflows/build.yml` は次のジョブ / matrix を持つ
  - `build-windows`: windows_x86_64
  - `build-macos`: macos_arm64
  - `build-ubuntu`: ubuntu-20.04_x86_64 / ubuntu-22.04_x86_64 / ubuntu-22.04_armv8_jetson / android
  - `create-release`: `ubuntu-22.04_armv8_jetson` のアーティファクトのみをダウンロードして Release にアップロードする
  - `notification`: `build-windows` / `build-macos` / `build-ubuntu` / `create-release` を `needs` に持つ
- 実際に Release にアップロードされるアセットは jetson のみであり、他プラットフォームのビルド結果は使われていない
- 対象は `support/jetson-jetpack-6` ブランチ (develop ではない)。Jetson 対応は `support/jetson-jetpack-5` / `support/jetson-jetpack-6` ブランチに切り出されており、develop の `run.py` からは Jetson 系ターゲットは削除済み
- `support/jetson-jetpack-5` には `.github/workflows/build.yml` がなく、本 issue の対象は `support/jetson-jetpack-6` のみ

## 設計方針

- `support/jetson-jetpack-6` の `.github/workflows/build.yml` から `build-windows` / `build-macos` を削除し、`build-ubuntu` の matrix を `ubuntu-22.04_armv8_jetson` のみにする
- `build-ubuntu` 内の他プラットフォーム向けの条件分岐 (`Install deps for ubuntu-...`、`Setup JDK` / `Setup Android SDK`、Examples のビルド条件など) を整理する
- `create-release` と `notification` の `needs` を更新する

## 完了条件

- `support/jetson-jetpack-6` の `.github/workflows/build.yml` が `ubuntu-22.04_armv8_jetson` のみをビルドすること
- タグ push 時に Release に jetson のアセットが従来どおりアップロードされること
- ビルド時間が短縮されること

## Pending 理由

- `support/jetson-jetpack-6` は実機 (Jetson) を必要とするサポートブランチであり、ビルドとリリースの確認に実機環境が必要
- 不急のサポートブランチ保守であり、優先度が確定していない

## Pending 解除条件

- jetson のみに絞ったワークフローで、タグ push から Release までを確認できる状態になったこと
