# BaseRenderer の枠割り計算のユニットテストを追加する

- Created: 2026-08-02
- Completed: {YYYY-MM-DD}
- Branch: feature/add-base-renderer-outline-tests
- Polished: {YYYY-MM-DD}
- Reporter: @voluntas

## 目的

`BaseRenderer::SetOutlines()` の枠割り計算 (cols/rows 決定後の共通縮小とウィンドウ中央寄せ、累積式による cell 座標算出、float クランプ) は純粋な数値計算であり、float 丸め・境界値・極端なアスペクトの組み合わせで枠が崩れやすいロジックである。しかし `test/` に BaseRenderer のテストが存在せず、この計算の変更 (バグ修正・リファクタリング) を自動検出できない。回帰テストを追加して、枠割りの変更を安全にする。

## 現状

- `test/` 配下に `BaseRenderer` / `SetOutlines` を参照するテストは存在しない (`test/CMakeLists.txt` に該当ターゲットなし)
- `BaseRenderer::SetOutlines()` は `BaseRenderer` の private メソッドであり、`Sink` も private なネストクラスであるため、テストから直接アクセスできない構造になっている
- 既存の E2E (`test/e2e.cpp`) と CI の E2E (sumomo) はレンダラーの枠割りを検証しない

## 設計方針

- 方針 B のグリッド計算 (幅・高さ・アスペクト・rows/cols から各 cell の offset と size を算出する部分) を、`BaseRenderer::SetOutlines()` から純粋関数として抽出してテスト可能にする
- または `BaseRenderer` を継承したテスト用クラスで `Render()` 経由の `SinkInfo` を収集し、枠割り結果を間接的に検証する
- どちらの方式を採用するかは実装時に判断する (既存の `SinkInfo` 構造体は公開されており、間接検証の観測点にできる)
- 検証する数値は、float32 での完全一致が期待できるケース (2560×1440 + 16:9 + sinks=2 → cols=2/rows=1、各 cell 1280×720、grid_offset_y=360) を中心に、クランプが発動するケースも含める

## 完了条件

- `BaseRenderer::SetOutlines()` の枠割り計算に対するユニットテストが `test/` に追加されていること
- 追加したテストがローカルビルド (`python3 run.py build --test --disable-cuda macos_arm64`) で通ること
- `python3 run.py format` で clang-format に差分が出ないこと
