# BaseRenderer の枠割り計算のユニットテストを追加する

- Created: 2026-08-02
- Completed: {YYYY-MM-DD}
- Branch: feature/add-base-renderer-outline-tests
- Polished: 2026-09-14
- Reporter: @voluntas

## 目的

`BaseRenderer::SetOutlines()` の枠割り計算 (cols/rows 決定後の共通縮小とウィンドウ中央寄せ、累積式による cell 座標算出、float クランプ) は純粋な数値計算であり、float 丸め・境界値・極端なアスペクトの組み合わせで枠が崩れやすいロジックである。しかし既存の `test/base_renderer.cpp` (closed の 0053 で追加) は `BaseRenderer::Sink::OnFrame()` のスケール経路と単一 Sink の枠割り結果しか検証しておらず、複数 Sink のグリッド計算の変更 (バグ修正・リファクタリング) を自動検出できない。回帰テストを追加して、枠割りの変更を安全にする。

## 現状

- `test/base_renderer.cpp` に `test/CMakeLists.txt` の `TEST_BASE_RENDERER` ターゲットとして追加されたテストが 5 ケース存在する。ただし全ケースが Sink 1 本の検証であり、`BaseRenderer::Sink::OnFrame()` のスケール経路 (拡大・縮小・letterbox・回転 90°・ゼロ寸法ガード) を検証するもので、複数 Sink でグリッドを組んだ場合の `BaseRenderer::SetOutlines()` の計算 (cols/rows の決定、cell 座標の累積算出、rows > 1 でのウィンドウ中央寄せ、float クランプ) は検証していない
- `BaseRenderer::SetOutlines()` は `BaseRenderer` の private メソッドであり、`Sink` も private なネストクラスであるため、テストから直接アクセスできない構造になっている
- 既存の E2E (`test/e2e.cpp`) と CI の E2E (`e2e-test/`) はレンダラーの枠割りを検証しない

## 設計方針

- グリッド計算 (幅・高さ・アスペクト・rows/cols から各 cell の offset と size を算出する部分) をテスト対象とし、`BaseRenderer` を継承したテスト用クラスで `Render()` 経由の `SinkInfo` を収集して枠割り結果を間接的に検証する。`SinkInfo` 構造体は公開されており、既存の `test/base_renderer.cpp` の `TestRenderer` がこの方式で枠割り結果を観測できることを実証済みである
- `BaseRenderer::SetOutlines()` から純粋関数を抽出する方式は、private メソッドの内部計算を公開ヘッダーに晒すことになり、本 issue のスコープを越えるため採用しない
- 検証する数値は、float32 での完全一致が期待できるケース (2560×1440 + 16:9 + sinks=2 → cols=2/rows=1、各 cell 1280×720、grid_offset_y=360) を中心に、クランプが発動するケースも含める

## 完了条件

- 複数 Sink (sinks >= 2) のグリッド計算に対する `BaseRenderer::SetOutlines()` の枠割り計算のユニットテストが `test/` に追加されていること (設計方針に挙げた 2560×1440 + 16:9 + sinks=2 のケースを含むこと)
- 追加したテストがローカルビルド (`python3 run.py build --test --disable-cuda macos_arm64`) で通ること
- `python3 run.py format` で clang-format に差分が出ないこと
