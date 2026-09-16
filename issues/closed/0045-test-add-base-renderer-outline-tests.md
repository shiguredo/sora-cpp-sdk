# BaseRenderer の枠割り計算のユニットテストを追加する

- Created: 2026-08-02
- Completed: 2026-09-16
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

## 解決方法

`test/base_renderer.cpp` に複数 Sink のグリッド計算を検証するテストケースを 5 件追加した。実装 (`src/renderer/base_renderer.cpp` / `include/sora/renderer/base_renderer.h`) は変更していない。

### テスト用ヘルパーの拡張

- `ExpectedRect` / `ActualRect` を追加し、`SinkInfo` の `offset_x` / `offset_y` / `frame_width` / `frame_height` を枠割りの期待値・実測値として比較する
- `TestRenderer` の公開 API は「描画結果の待ち合わせ (`WaitUntil()` / `WaitForSinkRects()` / `WaitForSinkRect()`)」と「描画結果の観測 (`GetActualRects()` / `RegionHasVideo()` / `RegionIsBlack()` / `CanvasWidth()` / `CanvasHeight()`)」に限定し、テスト専用の汎用機構を public に増やさないようにした。mutex 保持前提の内部ヘルパー (`GetActualRectsLocked()` / `RegionHasVideoLocked()`) は private に置く
- 既存の `WaitForSinkRect()` は `WaitForSinkRects()` の 1 要素版に置き換えた (既存 5 ケースの検証内容と期待値は不変)
- `TrackFixture` を `TrackFixture(renderer, track_count)` に拡張し、1 本のワーカースレッド上で複数の実トラックを `AddTrack()` して同じ寸法のフレームを継続注入する。モックやスタブは利用していない

### 追加したテストケース

- 2560x1440 / 16:9 / sinks=2: `cols=2` / `rows=1`、各セル 1280x720、`grid_offset_y=360` を検証する (完了条件のケース)
- 2560x1440 / 4:3 / sinks=2: 代表 Sink の実測アスペクト 4:3 が採用され、各セル 1280x960 (offset_y=240) になることを検証する。16:9 固定 (WIDE_ASPECT) のままでは 1280x720 になるため、`frame_aspect` の決定経路を守る
- 2560x1440 / 16:9 / sinks=4: `cols=2` / `rows=2` になり、3 番目以降のセルが 2 行目の先頭から cell 座標の累積式で配置されることを検証する
- 360x640 / 16:9 / sinks=2: ウィンドウが映像より縦長なため `cols=1` / `rows=2` になり、各セル 360x202 / 359x202 (理想枠の右端が float 丸めで 1 px 手前になる)、offset_y が 117 / 320 になることを検証する
- 1x1 / 16:9 / sinks=2: 極小ウィンドウで両方のセルが幅 0 に潰れる (1 つ目は 0x0、2 つ目は高さ 1 px) ため、0 寸法の Sink を描画対象に含めない設計により `SinkInfo` が空のまま描画スレッドが動き続けることを検証する。さらに `SetSize(2560, 1440)` で 2 つの映像が再び描画対象に戻ること (0 寸法の状態から復帰できること) を検証する

各ケースでは期待値の一致に加えて、観測した矩形がウィンドウの描画バッファ内に収まること (`offset >= 0` かつ `offset + 寸法 <= ウィンドウ`) と、`cols` 列のグリッドで同一行の隣接セルが隙間なく連続することを検証する。

### 検証結果

- `python3 run.py build --test --disable-cuda ubuntu-24.04_x86_64` でビルドし、`_build/ubuntu-24.04_x86_64/release/test/base_renderer` の 10 ケース 29 アサーションがすべて通ることを確認した (`--order rand` の 6 パターンでも安定)
- 回帰検出力を確認した。`frame_aspect` の実測アスペクト採用を外すと 4:3 のケースが失敗し、`col = i % cols` を変更すると 2x2 グリッドのケースが失敗する
- `python3 run.py format` で差分が出ないこと、`python3 run.py iwyu ubuntu-24.04_x86_64` が `test/base_renderer.cpp` に対して include の不足を指摘しないことを確認した

### 補足

issue の設計方針にある float クランプ (`std::max(0.0f, ...)` と `std::min((float)width_, ...)`) は、整数寸法のウィンドウと映像アスペクトの組み合わせでは実際には発動しない (float32 で `SetOutlines()` のアルゴリズムを再現して探索しても、クランプの有無で結果が変わらない)。クランプ自体は描画バッファ外への書き込みを防ぐ防御であり、テストでは「観測した矩形がウィンドウ内に収まること」を不変条件として検証する形にした。
