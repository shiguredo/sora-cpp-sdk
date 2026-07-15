# BaseRenderer の描画バッファがウィンドウリサイズ時に再確保されずセグフォする

- Priority: High
- Created: 2026-07-14
- Completed: {YYYY-MM-DD}
- Model: DeepSeek V4 Pro
- Branch: feature/fix-base-renderer-buffer-overflow-on-resize
- Polished: 2026-07-15

## 目的

`BaseRenderer::RenderThread()` で確保している描画用バッファがウィンドウサイズ変更時に再確保されない。sumomo の `--use-sdl` モードでウィンドウが拡大されると `width_` / `height_` だけが大きくなり、バッファは起動時サイズのまま残る。次回の描画フレームで `memset(image.get(), 0, width_ * height_ * 4)` が拡大後のサイズで実行され、バッファ境界を超過して write し、セグメンテーションフォールト（またはヒープ破壊）が発生する。

## 優先度根拠

sumomo の `--use-sdl` モードで、F キーによるフルスクリーン切替やウィンドウの手動拡大といったユーザー操作だけで、`width_` / `height_` がバッファ確保時より大きくなり確定的に落ちうる。毎フレームの `memset` は sinks 走査前に実行されるため、映像トラックの有無にかかわらずクラッシュする。High。

## 現状

`src/renderer/base_renderer.cpp` の `RenderThread()`:

```cpp
// 74 行目: バッファは起動時のサイズで一度だけ確保される
std::unique_ptr<uint8_t[]> image(new uint8_t[width_ * height_ * 4]);

while (running_) {
    // 77 行目: width_ / height_ が拡大されていても、確保済みサイズを超えて write する
    //         sinks 走査前のため、映像トラックが 0 本でも実行される（0 sinks でもクラッシュが確定する要因）
    memset(image.get(), 0, width_ * height_ * 4);

    {
        webrtc::MutexLock lock(&sinks_lock_);
        for (...) {
            // 92-102 行目: ローカル width/height は Sink のフレーム寸法。
            //            宛先 stride / Y offset にはメンバ width_ を使う。
            //            バッファ未再確保のまま width_ が拡大しているとここでも境界超過しうる
            int width = sink->GetFrameWidth();
            int height = sink->GetFrameHeight();
            libyuv::ARGBCopy(sink->GetImage(), width * 4,
                             image.get() + sink->GetOffsetX() * 4 +
                                 sink->GetOffsetY() * width_ * 4,
                             width_ * 4, width, height);
        }
    }

    // 117 行目: 拡大後の width_ / height_ で Render() にバッファを渡す
    Render(image.get(), width_, height_, sink_infos);
}
```

`SetSize()` (`base_renderer.cpp:64-68`) は `sinks_lock_` 取得下で `width_` / `height_` を更新し `SetOutlines()` を呼ぶだけで、描画バッファには触れない。

### 再現経路（sumomo `--use-sdl`）

1. 起動時、コンストラクタ引数のウィンドウサイズで `RenderThread()` がバッファを一度だけ確保する（例: 640×480）
2. 次のいずれかで `SDL_EVENT_WINDOW_RESIZED` が発生し、`SDLRenderer::PollEvent()` (`examples/sumomo/src/sdl_renderer.cpp:92-94`) が `SetSize()` を呼ぶ
   - F キー（`sdl_renderer.cpp:96-99`） → `SetFullScreen()` → `SDL_SetWindowFullscreen`（`sdl_renderer.cpp:80`）
   - ウィンドウ枠の手動ドラッグ（`SDL_WINDOW_RESIZABLE`、`sdl_renderer.cpp:38-39`）
   - `--fullscreen` 起動後、`dispatch_` 設定以降に初めて処理される RESIZED イベント
3. `width_` / `height_` が画面解像度相当に拡大する（例: 2560×1440）
4. 次回イテレーションの `memset` が確保済みバッファを超えて write する

`SetSize()` はメインスレッド、`RenderThread()` は専用 `std::thread`（`base_renderer.cpp:49`）で、sumomo では両者が並行しうる（描画スレッドから `dispatch_` 経由で `PollEvent` がメインスレッドに post される）。

### 再現条件

- sumomo を `--use-sdl` 付きで起動し、次のいずれかを行う（`--role` は sendonly / recvonly / sendrecv のいずれでも可）
  - F キーでフルスクリーン切替
  - ウィンドウを起動時より大きく手動リサイズ
  - `--fullscreen` 付きで起動し、表示後にリサイズイベントが処理されるのを待つ
- 手動確認コマンド例（リポジトリルートから。シグナリング URL / チャネル ID は環境に合わせる）。修正をバイナリに入れるため、ローカル SDK を紐付けてビルドする:
  ```bash
  python3 examples/sumomo/run.py build macos_arm64 --local-sora-cpp-sdk-dir .
  ./examples/_build/macos_arm64/release/sumomo/sumomo --use-sdl \
    --signaling-url <URL> --channel-id <ID> \
    --role recvonly --video-codec-type VP8
  ```

### 影響範囲

- **影響あり**: sumomo の `--use-sdl`（`examples/sumomo/src/sdl_renderer.cpp` が `BaseRenderer` を継承し、`SetSize()` を呼ぶ）
- **潜在リスクのみ**: Sixel / ANSI レンダラーは `BaseRenderer` を継承するが、現状 `SetSize()` を呼ぶ経路が無いため顕在化しない
- **影響なし**: `examples/sdl_sample` は `BaseRenderer` を継承せず独自実装（共有描画バッファを使わず各 Sink の `image_` を直接 SDL に渡す）のため本バグの対象外

### 本 issue のスコープ外

- メンバ `width_` / `height_` 自体の atomic 化や、`SetSize` 呼び出し規約の API 文書化など、**描画バッファ経路以外** の data race 設計。描画バッファ経路の寸法参照をスナップショットで直列化する作業は本バグ修正の本体でありスコープ内
- `width_ * height_ * 4` の包括的な整数オーバーフロー対策（`SIZE_MAX` 超過確認等）。既存コードからの持越しであり本 issue では扱わない。`resize()` の引数型 `size_t` に合わせる `static_cast<size_t>` は自然な型変換であり、包括的なオーバーフロー対策とは別物として本 issue で組み込む

## 設計方針

描画バッファを `std::vector<uint8_t>` にし、毎イテレーションで `sinks_lock_` 保持中にキャンバス寸法をスナップショットして `resize()` する。同一スナップショットを `memset` / `ARGBCopy` 宛先 / `Render()` に使い、バッファ実サイズと write サイズを一致させる。

ロック外で `resize` と `memset` がそれぞれ `width_` を読む実装では、両者の間に `SetSize` が入り再確保後より大きいサイズで write する穴が残る。`SetSize` も `sinks_lock_` を取るため、バッファ操作を同ロック下に置けば寸法の読み書きが直列化され、境界超過は閉じる。フルスクリーン時の再確保（例: 数 MB〜十数 MB）でロック保持時間は増えるが、寸法一貫性のための意図的な選択であり許容する。

`Render()` 仮想関数のシグネチャは変更しない。`vector::data()` を渡す。サブクラス変更は不要。公開ヘッダの ABI 影響なし。`Render()` 自体は現行どおりロック外で呼ぶ（SDL 操作と `dispatch_` を `sinks_lock_` 内に入れると、同期 dispatch 化された場合に自己デッドロックしうる）。

## 完了条件

- `src/renderer/base_renderer.cpp` の `RenderThread()` について、次をすべて満たすこと
  1. 描画バッファが `std::vector<uint8_t>` になっている。`while` ループの外で宣言し、イテレーション間で capacity を再利用する
  2. 毎イテレーション、キャンバス寸法を `canvas_width` / `canvas_height`（Sink フレーム寸法の `width` / `height` と別名）にスナップショットしている
  3. `sinks_lock_` 保持中に `image.resize(static_cast<size_t>(canvas_width) * static_cast<size_t>(canvas_height) * 4)` と `memset(image.data(), 0, image.size())` を行う
  4. `ARGBCopy` の宛先 stride / Y offset と `Render(...)` 引数が **同一の `canvas_width` / `canvas_height`** のみを使う（`resize` 後に `width_` を再読しない）
  5. Sink フレーム寸法用のローカル `width` / `height`（`GetFrameWidth()` / `GetFrameHeight()`）は現状どおり残し、キャンバス寸法と混同しない
  6. `GetOutlineChanged()` が true の Sink を `continue` する現行分岐を維持する（outline 再計算中の古いフレームを新バッファへ copy しない）
  7. `Render()` は `sinks_lock_` 外で呼ぶ（理由は設計方針を参照）
- 既存 E2E の回帰がないこと:
  ```bash
  uv run --directory=e2e-test pytest \
    test_sumomo_basic.py::test_sumomo_sendonly_recvonly[VP8] -v -s --timeout=60
  ```
  （フルスクリーン / リサイズの自動テストは GUI 操作が必要で現状存在しない。本バグ自体の検証は手動とする）
- 手動確認（上記の `--local-sora-cpp-sdk-dir .` 付きビルド成果物を使う。シグナリングサーバーは既存 E2E と同一でよい）:
  - `--use-sdl` で起動し、F キーによるフルスクリーン切替（入る・戻る）を 3 回以上繰り返してもクラッシュしない
  - ウィンドウを起動時より大きく手動リサイズしてもクラッシュしない
  - フルスクリーン解除後も映像表示が継続する
- `CHANGES.md` の `## develop` 直下（`### misc` より前のコア `[FIX]` 群）に追記する。`src/renderer/base_renderer.cpp` は core SDK コードのため `## develop` 直下に置く。担当者は実装者が確定:
  ```
  - [FIX] BaseRenderer の描画バッファがウィンドウリサイズ時に再確保されずセグフォする問題を修正する
    - @<担当者>
  ```

## 解決方法

`src/renderer/base_renderer.cpp` の `RenderThread()` を次の形に書き換える（`<vector>` および `<cstring>` は既に include 済み）。

```cpp
void BaseRenderer::RenderThread() {
  RenderThreadStarted();

  std::vector<uint8_t> image;

  while (running_) {
    auto frame_start = std::chrono::steady_clock::now();
    std::vector<SinkInfo> sink_infos;
    // ロック外で宣言し、ロック内で代入して Render まで持ち出す
    int canvas_width = 0;
    int canvas_height = 0;
    {
      webrtc::MutexLock lock(&sinks_lock_);
      canvas_width = width_;
      canvas_height = height_;
      // resize の引数型 size_t に合わせて乗算を行う（int 乗算後の cast にしない）
      image.resize(static_cast<size_t>(canvas_width) * static_cast<size_t>(canvas_height) * 4);
      // resize の拡大分ゼロ初期化だけでは既存要素が残るため、フレーム全体をクリアする
      memset(image.data(), 0, image.size());

      for (const VideoTrackSinkVector::value_type& sinks : sinks_) {
        Sink* sink = sinks.second.get();
        webrtc::MutexLock frame_lock(sink->GetMutex());
        if (sink->GetOutlineChanged()) {
          continue;
        }
        // Sink フレーム寸法（キャンバス寸法 canvas_* と別名を維持する）
        int width = sink->GetFrameWidth();
        int height = sink->GetFrameHeight();
        if (width == 0 || height == 0) {
          continue;
        }
        // ソース stride: width * 4（Sink フレーム幅）
        // 宛先 stride / Y offset: canvas_width（キャンバス幅）
        libyuv::ARGBCopy(sink->GetImage(), width * 4,
                         image.data() + sink->GetOffsetX() * 4 +
                             sink->GetOffsetY() * canvas_width * 4,
                         canvas_width * 4, width, height);

        SinkInfo info;
        info.offset_x = sink->GetOffsetX();
        info.offset_y = sink->GetOffsetY();
        info.input_width = sink->GetInputWidth();
        info.input_height = sink->GetInputHeight();
        info.frame_width = sink->GetFrameWidth();
        info.frame_height = sink->GetFrameHeight();
        info.width = sink->GetWidth();
        info.height = sink->GetHeight();
        sink_infos.push_back(info);
      }
    }

    // width_ / height_ を再読しない
    Render(image.data(), canvas_width, canvas_height, sink_infos);

    // フレームレート制御ロジックは現行どおり（frame_start をループ先頭に移動し、resize / memset コストも elapsed に含まれる）
    auto frame_end = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                       frame_end - frame_start)
                       .count();
    int frame_interval = 1000 / fps_;
    if (elapsed < frame_interval) {
      std::this_thread::sleep_for(
          std::chrono::milliseconds(frame_interval - elapsed));
    }
  }

  RenderThreadFinished();
}
```

### 注意点

- `resize()` が内部バッファを再確保した場合、それ以前の `data()` は無効になる。必ず `resize()` の後で `data()` を取得する。現行は `unique_ptr` でポインタが全イテレーションを通じて固定だが、修正後は `vector::resize()` の再確保でイテレーション間で `data()` が変化しうる。`Render()` は返るまでにバッファを使い切る前提（現行サブクラスは同期利用）であり、`Render()` 内でポインタを保存してはならない
- `canvas_width` / `canvas_height` が 0 の場合も安全側に倒れる（`resize(0)` は no-op、sinks は outline 変更またはフレーム寸法 0 で skip され、`Render` は空バッファで呼ばれるが現行サブクラスは安全に処理する。SDL が 0×0 の RESIZED を送ることは現実的にはない）
- `Sink::image_` は `OnFrame()` 内で outline / 入力解像度変更時に再確保されており、本バグの原因ではない。修正対象外
- 縮小時は `vector` の capacity が維持され、ピーク解像度分のメモリが残る。デスクトップ向けサンプルを主用途とする本 SDK では許容し、`shrink_to_fit()` は呼ばない
- ロック順序は現行どおり `sinks_lock_` → `frame_params_lock_` であり、`SetSize` / `OnFrame` とのデッドロックは増えない
