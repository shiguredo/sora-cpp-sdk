# BaseRenderer の未使用フィールドを削除する

- Created: 2026-08-02
- Completed: {YYYY-MM-DD}
- Branch: feature/refactor-remove-base-renderer-dead-code
- Polished: {YYYY-MM-DD}
- Reporter: @voluntas

## 目的

`BaseRenderer` と `BaseRenderer::Sink` に write-only の未使用フィールドが残っており、コードの可読性と保守性を損なっている。実体のない状態管理を削除して、フィールドの意味を把握しやすくする。

## 現状

- `Sink::renderer_` (include/sora/renderer/base_renderer.h の `Sink` クラス宣言、src/renderer/base_renderer.cpp の `Sink::Sink()` 初期化リスト): コンストラクタで代入されるだけで、リポジトリ内に読み出し箇所が一切ない write-only フィールド
- `BaseRenderer::rows_` / `BaseRenderer::cols_` (ヘッダのメンバ宣言、コンストラクタ初期化リスト、`SetOutlines()` 末尾の代入): 設定されるだけで読み出し箇所がない write-only フィールド。`SetOutlines()` のローカル変数 `rows` / `cols` だけで完結している

## 設計方針

- 該当フィールドの宣言・初期化リスト・代入を削除する
- `Sink` コンストラクタの `renderer` 引数はフィールド削除後も呼び出し側との整合を確認し、不要になるなら引数ごと削除する (呼び出し箇所は `BaseRenderer::AddTrack()` の `new Sink(this, track)` のみ)
- 挙動は一切変えない (フィールドは読み出されないため、削除しても出力は不変)

## 完了条件

- `renderer_` / `rows_` / `cols_` の宣言・代入が削除されていること
- ローカルビルド (`python3 run.py build --test --disable-cuda macos_arm64`) と既存テストが通ること
- `python3 run.py format` で clang-format に差分が出ないこと
