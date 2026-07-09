# Intel VPL を v2.17.0 にあげる

- Priority: Medium
- Created: 2026-06-26
- Completed: {YYYY-MM-DD}
- Model: glm-5.2
- Branch: feature/change-update-intel-vpl-v2.17.0
- Polished: 2026-07-08

## 目的

Intel VPL (libvpl) の最新版 v2.17.0 が 2026-06-25 にリリースされている。
現在は v2.16.0 を使用中のため、最新版に追従する。

v2.17.0 の主な変更:
- Intel VPL API 2.17 サポート
  - デコーダ能力レポートの新 API
  - ビデオメモリ上のビットストリームバッファアクセス
  - エンコーダのプリプロセス設定
  - ドキュメント更新

## 優先度根拠

Medium。バグ修正や緊急対応ではなく、依存ライブラリの定期的なバージョン追従であるため。機能面での影響はなく、API 2.17 の新機能を sora-cpp-sdk 側で即座に利用する必要もない。

## 設計方針

`DEPS` の `VPL_VERSION` を `v2.16.0` から `v2.17.0` に変更するのみ。`buildbase.py` の `install_vpl` 関数に変更は不要で、ビルドオプションもそのまま利用可能。

## 完了条件

- `DEPS` の `VPL_VERSION` が `v2.17.0` になっていること
- Windows / Ubuntu の両方でビルドが通ること
- E2E テスト `test_sumomo_intel_vpl.py` が通ること
- `CHANGES.md` に `[UPDATE] Intel VPL を v2.17.0 にあげる` のエントリが追加されていること

## 解決方法

CI ([#279](https://github.com/shiguredo/sora-cpp-sdk/actions/runs/28917520757)) がすべて成功していることを確認済み

1. `DEPS` の `VPL_VERSION` を `v2.16.0` → `v2.17.0` に変更する
2. `buildbase.py` でビルドして Windows / Ubuntu 両方でビルドが通ることを確認する
3. E2E テスト `test_sumomo_intel_vpl.py` を実行してデコーダ/エンコーダが正常動作することを確認する
4. `CHANGES.md` にエントリを追加する
