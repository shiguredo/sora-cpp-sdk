# BaseRenderer の描画バッファがウィンドウサイズ変更時に再確保されず、フルスクリーン切替時にセグフォする

- Priority: High
- Created: 2026-07-14
- Completed: {YYYY-MM-DD}
- Model: DeepSeek V4 Pro
- Branch: feature/fix-base-renderer-buffer-overflow-on-resize
- Polished: 2026-07-14

## 目的

`BaseRenderer::RenderThread()` で確保している描画用バッファがウィンドウサイズ変更時に再確保されない。sumomo の `--use-sdl` モードで F キーを押してフルスクリーンを切り替えると、ウィンドウがリサイズされ `width_` / `height_` が大きくなるが、バッファは起動時のサイズのままである。次回の描画フレームで `memset(image.get(), 0, width_ * height_ * 4)` および `libyuv::ARGBCopy()` が拡大後のサイズで実行され、バッファ境界を超過して write し、セグメンテーションフォールトが発生する。

## 優先度根拠

sumomo の `--use-sdl` モードで F キーを押すだけで再現する。`--role` が sendonly / recvonly / sendrecv のいずれであっても、`--use-sdl` が有効であれば `BaseRenderer` が初期化されバッファが確保されるため、映像トラックの有無にかかわらずクラッシュする。ユーザーの操作によって確定的にアプリケーションが落ちる。High。

## 現状

`src/renderer/base_renderer.cpp` の `RenderThread()` 関数:

```cpp
// 74 行目: バッファは起動時のサイズで一度だけ確保される
std::unique_ptr<uint8_t[]> image(new uint8_t[width_ * height_ * 4]);

while (running_) {
    // 77 行目: width_ / height_ が拡大されていても、確保済みサイズを超えて write する
    memset(image.get(), 0, width_ * height_ * 4);

    // ...

    {
        webrtc::MutexLock lock(&sinks_lock_);
        for (...) {
            // 99-102 行目: ARGBCopy も同様に image バッファへ write するため、
            //            width_ 拡大時にバッファ境界を超過する
            libyuv::ARGBCopy(sink->GetImage(), width * 4,
                             image.get() + offset,
                             width_ * 4, width, height);
        }
    }

    // 117 行目: 拡大後の width_ / height_ で Render() にバッファを渡す
    Render(image.get(), width_, height_, sink_infos);
}
```

### クラッシュまでの流れ

1. sumomo 起動時、ウィンドウサイズに応じたバッファを確保 (例: 640×480×4 = 1,228,800 bytes)
2. F キー押下 → `SDLRenderer::PollEvent()` → `SetFullScreen()` → `SDL_SetWindowFullscreen(window_, true)`
3. フルスクリーン化に伴い `SDL_EVENT_WINDOW_RESIZED` が発生 → `SetSize()` が呼ばれ `width_` / `height_` が画面解像度相当に拡大 (例: 2560×1440)
4. 次回 `RenderThread()` のイテレーションで `memset(image.get(), 0, 2560 * 1440 * 4)` が 1,228,800 bytes のバッファ境界を超過 write → セグフォ

### 再現条件

- sumomo を `--use-sdl` 付きで起動し、F キーでフルスクリーン切替を行う。`--role` は sendonly / recvonly / sendrecv のいずれでも再現する
- macOS では特に、Metal バックエンドでのフルスクリーン遷移時に解像度が大きく変わるため高確率で再現する
- 手動確認コマンド例: `python3 examples/sumomo/run.py build macos_arm64 && ./_build/macos_arm64/sumomo/sumomo --use-sdl --signaling-url <URL> --channel-id <ID> --role recvonly --video-codec-type VP8`

### 影響範囲

- sumomo の `--use-sdl` モード (`examples/sumomo/src/sdl_renderer.cpp` が `BaseRenderer` を継承)
- `sdl_sample` (`examples/sdl_sample/src/sdl_renderer.cpp`) は `BaseRenderer` を継承しておらず、独自の描画ループで Sink ごとに都度バッファ確保するため影響なし
- Sixel / ANSI レンダラーは `BaseRenderer` を継承しているが、現在これらのレンダラーには `SetSize()` を呼び出す機構が存在しないため本バグの影響は顕在化しない。ただし `BaseRenderer` レベルのバッファ脆弱性として潜在リスクは残る

### 本 issue のスコープ外

- `width_` / `height_` の data race: `SetSize()` は `sinks_lock_` を取得して書き込む一方、`RenderThread()` 内の `width_` / `height_` 読み取りはロック外で行われている。これは既存の設計上の問題であり、修正の影響範囲とリスクを最小限に留めるため本 issue では対応しない。別 issue で扱う
- `width_ * height_ * 4` の整数オーバーフロー: `width_` / `height_` が `int` 型のため、極端に大きなウィンドウサイズでは乗算結果がオーバーフローしうる。既存コードからの持越し問題であり本 issue のスコープ外とする

## 設計方針

`RenderThread()` 内の `std::unique_ptr<uint8_t[]>` によるバッファを `std::vector<uint8_t>` に変更し、毎イテレーション開始時に `width_ * height_ * 4` で `resize()` する。`BaseRenderer` の `Render()` 仮想関数のシグネチャ (`uint8_t* image, int width, int height`) は変更せず、`vector` の内部ポインタを `image` 引数として渡す。

### 具体的な変更内容

`src/renderer/base_renderer.cpp` の `RenderThread()` 関数:

1. 変数宣言を `std::unique_ptr<uint8_t[]> image(new uint8_t[...])` から `std::vector<uint8_t> image` に変更する
2. `while (running_)` ループの先頭（`memset` の直前）に `image.resize(static_cast<size_t>(width_) * height_ * 4)` を追加する。`resize()` はサイズ変更がない場合は size 比較のみで no-op 相当であり、毎フレーム呼び出しのパフォーマンス影響は無視できる。size が拡大された場合、旧 size を超える追加要素はゼロ初期化されるが、既存要素はそのまま残る。縮小時は capacity が維持され、メモリ解放は行われない
3. `memset(image.get(), 0, ...)` → `memset(image.data(), 0, static_cast<size_t>(width_) * height_ * 4)` に変更する。`memset` の第三引数は `size_t` のため、`resize()` と同様に `static_cast<size_t>` を使用する。`memset` は `resize()` では既存要素がゼロクリアされないため、バッファ全体をゼロクリアする目的で機能的に必須であり、維持する
4. `ARGBCopy` の宛先ポインタ `image.get() + offset` → `image.data() + offset` に変更する（`base_renderer.cpp:100`）
5. `Render(image.get(), ...)` → `Render(image.data(), ...)` に変更する（`base_renderer.cpp:117`）

### 注意点

- `resize()` が実際に内部バッファを再確保した場合、それ以前に取得した `data()` の戻り値は無効化される。必ず `resize()` の**後で** `data()` を取得すること。ループ外でポインタをキャッシュしてはならない
- `Sink::image_`（`include/sora/renderer/base_renderer.h`）は `BaseRenderer::Sink` が個別に管理するバッファであり、`OnFrame()` 内で outline 変更時に正しく再確保されている。本バグは `RenderThread()` 側の描画用バッファに起因するため、`Sink::image_` は修正対象外とする
- 縮小時はメモリが解放されずピーク使用量が維持される。メモリ制約の厳しい環境では問題になりうるが、フルスクリーン切替後の最大解像度バッファを保持し続けることは許容範囲と判断する。`shrink_to_fit()` は呼ばない
- `SetSize()` は public API だが、SDL 経由の通常フローでは `Render()` → `dispatch_` → `PollEvent()` → `SetSize()` というチェーンで呼ばれ、同一フレームの `Render()` 完了後かつ次フレームの `resize()` 前に実行されるため、本修正後のコードパスにおいて TOCTOU 競合は発生しない。ただし `SetSize()` が外部スレッドから直接呼ばれた場合の保護は別途検討が必要である

## 完了条件

- `src/renderer/base_renderer.cpp` の `RenderThread()` 内のバッファが `std::vector<uint8_t>` に変更され、上記「具体的な変更内容」の 1〜5 がすべて実施されていること
- 既存の E2E テストが通ること（`test_sumomo_basic.py` 等）。フルスクリーン切替の自動テストは現時点で存在しないため、以下の手動確認で代替する（シグナリングサーバーは既存の E2E テスト (`test_sumomo_basic.py`) と同一のものを使用する）:
  - sumomo を `--use-sdl` 付きでビルドし、F キーによるフルスクリーン切替（入る・戻る）を 3 回以上繰り返してもクラッシュしないこと
  - フルスクリーン解除後も映像表示が継続すること
- `CHANGES.md` の `## develop` の先頭に `[FIX]` エントリを追記する（`### misc` ではなく `## develop` 直下にフラットに追記）。エントリの詳細な内容（変更箇所・件数等）は実装時に確定する:
  ```
  - [FIX] sumomo の SDL モードで F キーによるフルスクリーン切替時にセグフォする問題を修正する
    - @<担当者>
  ```
