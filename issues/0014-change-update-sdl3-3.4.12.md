# SDL3 を 3.4.12 にあげる

- Priority: Medium
- Created: 2026-07-09
- Model: deepseek-v4-flash
- Branch: feature/change-update-sdl3-3.4.12
- Polished: {YYYY-MM-DD}

## 目的

SDL3 の最新安定版 3.4.12 が 2026-07-01 にリリースされている。
現在は 3.2.28 を使用中のため、最新版に追従する。

3.2.28 から 3.4.12 までの主な変更:

3.4.0 (メジャーアップデート):
- GPU API と 2D レンダリング API の相互運用性向上 (SDL_CreateGPURenderer 等)
- PNG 画像のネイティブ読み書き対応 (SDL_LoadPNG, SDL_SavePNG 等)
- Emscripten 対応向上 (SDL_WINDOW_FILL_DOCUMENT 等)
- ペン入力処理の強化 (SDL_GetPenDeviceType 等)
- ピンチジェスチャイベント追加 (SDL_EVENT_PINCH_BEGIN 等)
- アニメーションカーソル対応 (SDL_CreateAnimatedCursor)
- SDL_SCALEMODE_PIXELART 追加
- ウィンドウ進行状況表示対応 (SDL_SetWindowProgressState 等)

3.4.2 - 3.4.12 (バグフィックス):
- Vulkan/Metal GPU レンダラのクラッシュ修正
- Windows コントローラ関連のクラッシュ修正 (3.4.6, 3.4.8)
- Wayland 対応改善 (カーソルスケール、外部サーフェス等)
- Xbox コントローラ軸が macOS で反転する問題の修正 (3.4.12)
- その他多数の安定性向上

## 優先度根拠

Medium。バグ修正や緊急対応ではなく、依存ライブラリの定期的なバージョン追従であるため。
3.4.0 で追加された API を sora-cpp-sdk のサンプルで即座に利用する必要もないが、複数のバグフィックスが含まれており、特に macOS のビルド互換性向上が期待できる (CHANGES.md で macos-14 での SDL3 ビルドエラーが報告されているため)。

## 設計方針

`examples/DEPS` の `SDL3_VERSION` を `3.2.28` から `3.4.12` に変更するのが基本。
サンプルコード (sdl_sample, sumomo) で使用している SDL3 API はコア API のみで、3.2.28 と 3.4.12 の間で破壊的変更は確認されていない。
`buildbase.py` の `install_sdl3` 関数の CMake オプションも現状のままで問題ない見込み。

### 事前調査結果: 破壊的変更の有無

#### CMake オプション
3.2.28 と 3.4.12 の `option(SDL_...)` 一覧を比較した結果、`buildbase.py` で使用している全オプション (`SDL_AUDIO`, `SDL_VIDEO`, `SDL_RENDER`, `SDL_OPENGL`, `SDL_OPENGLES`, `SDL_METAL`, `SDL_VULKAN`, `SDL_JOYSTICK`, `SDL_HAPTIC`, `SDL_POWER`, `SDL_SENSOR`, `SDL_KMSDRM`, `SDL_RPI`, `SDL_WAYLAND`, `SDL_X11`, `SDL_X11_SHARED`, `SDL_X11_XCURSOR`, `SDL_X11_XDBE`, `SDL_X11_XFIXES`, `SDL_X11_XINPUT`, `SDL_X11_XRANDR`, `SDL_X11_XSCRNSAVER`, `SDL_X11_XSHAPE`, `SDL_X11_XSYNC`, `SDL_STATIC`, `SDL_SHARED`) に**削除・リネーム・意味変更はなし**。以下が 3.4.12 で追加された新オプションだが、いずれもデフォルト OFF または opt-in で既存ビルドに影響しない:

- `SDL_DLOPEN_NOTES` - dlopen 依存関係を ELF ノートに記録
- `SDL_FRIBIDI` / `SDL_FRIBIDI_SHARED` - Fribidi サポート
- `SDL_LEAN_AND_MEAN` - リーンモード
- `SDL_LIBTHAI` / `SDL_LIBTHAI_SHARED` - Thai 言語サポート
- `SDL_X11_XTEST` - XTest サポート

`SDL_VULKAN` の条件に `OPENBSD` が追加されたが、`buildbase.py` では `-DSDL_VULKAN=OFF` と明示しているため影響なし。

#### 公開ヘッダ
3.2.28 → 3.4.12 で追加されたヘッダは `SDL_dlopennote.h` のみ。サンプルコードがインクルードしている全ヘッダ (`SDL_render.h`, `SDL_thread.h`, `SDL_video.h`, `SDL_error.h`, `SDL_events.h`, `SDL_init.h`, `SDL_keycode.h`, `SDL_mouse.h`, `SDL_pixels.h`, `SDL_rect.h`, `SDL_surface.h`, `SDL_timer.h`) は両バージョンで存在し、削除されたヘッダはない。

#### サンプルコードが使用している SDL3 API
以下の API はいずれも 3.2.28 と 3.4.12 の間で**シグネチャ変更なし**。`SDL_CreateSurfaceFrom(int width, int height, SDL_PixelFormat format, void *pixels, int pitch)` のシグネチャも同一 (`SDL_surface.h` の実装コメントで確認済み)。

- `SDL_Init`, `SDL_GetError`, `SDL_CreateWindow`, `SDL_CreateRenderer`
- `SDL_CreateThread`, `SDL_WaitThread` (sdl_sample のみ)
- `SDL_DestroyRenderer`, `SDL_DestroyWindow`, `SDL_Quit`
- `SDL_GetWindowFlags`, `SDL_SetWindowFullscreen`
- `SDL_HideCursor`, `SDL_ShowCursor`, `SDL_PollEvent`
- `SDL_GetWindowID`, `SDL_SetRenderDrawColor`, `SDL_GetTicks` (sdl_sample のみ)
- `SDL_RenderClear`, `SDL_CreateSurfaceFrom`, `SDL_CreateTextureFromSurface`
- `SDL_DestroySurface`, `SDL_RenderTexture`, `SDL_DestroyTexture`
- `SDL_RenderPresent`, `SDL_Delay` (sdl_sample のみ)

#### 結論
`examples/DEPS` のバージョン番号変更のみで対応可能。サンプルコードの修正は不要。

#### ビルド影響
- SDL3 自体のビルド: 全 CMake オプション互換。新たに追加された `SDL_LEAN_AND_MEAN` 等はデフォルト OFF のため影響しない
- サンプルコードのコンパイル: 全インクルードヘッダが存続、全 API シグネチャ変更なしのため修正不要
- リンク: リンク対象ライブラリに変更なし

#### 動作確認の注意点
コードを精査した結果、以下3点が潜在的なリスク領域:

1. **SDL_CreateRenderer(window_, NULL) のレンダラー選択**: 3.4.0 で `SDL_RENDER_GPU` (GPU バックエンドを使う新レンダラー) が追加された。従来の Metal/OpenGL/D3D に加えてこの GPU レンダラーが候補に加わるため、レンダリングの内部動作が変わる可能性がある。ただし `SDL_VULKAN=OFF` 設定の環境では GPU レンダラーは有効にならず、従来通り Metal/OpenGL/D3D が使われる見込み (`SDL_RENDER_GPU` には `SDL_GPU` が必要だが、SDL_GPU は Vulkan/Metal/D3D12 経由で動作する)。

2. **マルチスレッドの SDL_CreateRenderer**: macOS 以外のプラットフォームでは `SDL_CreateRenderer` を `SDL_CreateThread` で作成した描画スレッド内で呼んでいる。SDL3 のスレッド安全性要件に変更がないかは実際の動作確認が必要。

3. **SDL_Delay / SDL_GetTicks のタイミング精度**: 30fps のフレームレート制御に使われている。SDL3 実装のタイマー精度に変更があればフレームレートに影響するが、大幅な変更は報告されていない。

#### 動作確認手順
1. macOS で `sdl_sample` を起動し、ウィンドウ表示・Fキーフルスクリーン・Qキー終了を確認
2. macOS で `sdl_sample` の映像レンダリングに色味やアスペクト比の異常がないか目視確認
3. Windows / Linux でも同様の動作確認
4. `sumomo --no-video --no-audio` で最低限のウィンドウ表示確認

### スコープ外

- SDL 2.x 対応 (`install_sdl2` は既に使われていない名残のため本 issue では触れない)
- SDL3 3.4.x の新機能をサンプルコードで利用する対応

## 完了条件

- `examples/DEPS` の `SDL3_VERSION` が `3.4.12` になっていること
- macOS / Windows / Linux / Ubuntu ARMv8 の各プラットフォームでビルドが通ること
- `sdl_sample` が正しく動作すること
- `sumomo` の `--no-video --no-audio` モードなどで最低限の SDL ウィンドウ表示が動作すること
- `CHANGES.md` に `[UPDATE] SDL3 を 3.4.12 にあげる` のエントリが追加されていること

## 解決方法

1. `examples/DEPS` の `SDL3_VERSION` を `3.2.28` → `3.4.12` に変更する
2. macOS でローカルビルドし、sdl_sample が動作することを確認する
3. `buildbase.py` の CMake オプションに新規オプションや廃止オプションがないか確認する
4. CI で全プラットフォームのビルドが通ることを確認する
5. `CHANGES.md` にエントリを追加する
