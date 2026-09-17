# BaseRenderer の未使用フィールドを削除する

- Created: 2026-08-02
- Completed: 2026-09-17
- Branch: feature/refactor-remove-base-renderer-dead-code
- Polished: 2026-09-14
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

## 解決方法

`Sink::renderer_`、`BaseRenderer::rows_` / `cols_` の宣言と代入を削除した。

- `include/sora/renderer/base_renderer.h` : `Sink` の `BaseRenderer* renderer_` メンバと、`BaseRenderer` の `rows_` / `cols_` メンバを削除した。`renderer_` の削除で未使用になる `Sink` コンストラクタの `renderer` 引数も削除した (`Sink` は `BaseRenderer` の private なネストクラスであり、呼び出し箇所は `BaseRenderer::AddTrack()` の 1 箇所のみ)
- `src/renderer/base_renderer.cpp` : `BaseRenderer` と `Sink` のコンストラクタ初期化リストから該当フィールドを削除し、`SetOutlines()` 末尾の `rows_ = rows` / `cols_ = cols` も削除した。`SetOutlines()` のグリッド計算はローカル変数の `rows` / `cols` だけで完結しており、削除後も `cols` は列数、`rows` は行数として `SetOutlineRect()` の引数計算に使われている
- `BaseRenderer::AddTrack()` の Sink 生成を `new Sink(track)` に変更した
- 変更履歴 (`CHANGES.md`) の `## develop` の `### misc` に `[UPDATE]` エントリを追記した

削除したフィールドはいずれも書き込みのみで読み出し箇所がないため、描画結果と枠割りの挙動は変わらない。

### 検証結果

- `python3 run.py build ubuntu-24.04_x86_64 --test --disable-cuda` が成功した (作業環境が Ubuntu 24.04 x86_64 のため、完了条件の `macos_arm64` から読み替えた)
- `_build/ubuntu-24.04_x86_64/release/test/base_renderer` の 10 ケース 29 アサーションがすべて通ることを 3 回繰り返して確認した
- `grep` で `renderer_` / `rows_` / `cols_` の残存参照が 0 件であることを確認した
- `python3 run.py format` で clang-format に差分が出ないことを確認した
