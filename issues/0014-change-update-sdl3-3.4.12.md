# SDL3 を 3.4.12 にあげる

- Priority: Medium
- Created: 2026-07-09
- Completed: {YYYY-MM-DD}
- Model: deepseek-v4-flash
- Branch: feature/update-sdl3-3.4.12
- Polished: 2026-07-13
## 目的

SDL3 の最新安定版 3.4.12 が 2026-07-01 にリリースされた。
現在は 3.2.28 を使用しているため、最新版に追従する。

3.2.28 から 3.4.12 の間には 3.4.0 メジャーアップデートを含む複数リリースがある。
sora-cpp-sdk のサンプルではコア API のみ使用しており、破壊的変更はないが、
SDL 3.4.0 で新規追加された `SDL_X11_XTEST` オプション（デフォルト ON）が
Linux ビルド環境で `libXtst-dev` 非インストール時にビルドエラーとなるため対応が必要。

## 優先度根拠

Medium。バグ修正や緊急対応ではなく、依存ライブラリの定期的なバージョン追従であるため。
3.4.0 で追加された API を sora-cpp-sdk のサンプルで即座に利用する必要はないが、
3.4.2〜3.4.12 では SDL 内部の安定性向上やバグ修正が多数含まれており、間接的な品質向上が期待できる。

## 設計方針

`examples/DEPS` の `SDL3_VERSION` を `3.2.28` から `3.4.12` に変更する。
`buildbase.py` の `install_sdl3` 関数の Linux 用 CMake オプションに `-DSDL_X11_XTEST=OFF` を追加する。

### 事前調査結果

#### CMake オプション互換性

3.2.28 と 3.4.12 で `buildbase.py` が使用している全 CMake オプションに削除・リネーム・意味変更はない。
3.4.12 で追加された新オプションのうち影響があるのは `SDL_X11_XTEST` のみ:

- `SDL_X11_XTEST` - XTest サポート。**デフォルト ON**。Linux ビルド環境に `libXtst-dev` がインストールされていない場合、`SDL_missing_dependency(XTEST)` でビルドエラーになる。`buildbase.py` の Linux セクションに `-DSDL_X11_XTEST=OFF` を追加して回避する。

その他の新オプション（`SDL_DLOPEN_NOTES`、`SDL_FRIBIDI`、`SDL_LEAN_AND_MEAN`、`SDL_LIBTHAI` 等）はデフォルト OFF または opt-in のため既存ビルドに影響しない。

#### 公開ヘッダ互換性

3.2.28 → 3.4.12 で追加されたヘッダは `SDL_dlopennote.h` のみ。
サンプルコードがインクルードしている既存ヘッダに削除・リネームはない。

#### API シグネチャ互換性

サンプルコードが使用している全 SDL3 API に 3.2.28 → 3.4.12 の間でシグネチャ変更はない。

#### 結論

`examples/DEPS` のバージョン番号変更と `buildbase.py` への `-DSDL_X11_XTEST=OFF` 追加で対応可能。サンプルコードの修正は不要。

#### 潜在的なリスク領域

1. **`SDL_CreateRenderer(window_, NULL)` のレンダラー選択**: SDL 3.4.0 で `SDL_RENDER_GPU` が追加され、レンダラー選択の候補に加わった。macOS では `SDL_METAL=ON` のため Metal 経由で `SDL_GPU` サブシステムが有効化され `SDL_RENDER_GPU` が選択される可能性がある。レンダリングの内部動作が変わる可能性があるため、動作確認で目視検証が必要。

2. **マルチスレッドの `SDL_CreateRenderer`**: macOS 以外のプラットフォームでは `sdl_sample` と `sumomo` の両方で `SDL_CreateRenderer` を `SDL_CreateThread` で作成した描画スレッド内で呼んでいる。SDL3 のスレッド安全性要件に変更がないか実際の動作確認が必要。

3. **`SDL_GetTicks` / `SDL_Delay` のタイミング精度** (`sdl_sample` のみ): 30fps のフレームレート制御に使われている。SDL3 実装のタイマー精度に変更があればフレームレートに影響するが、大幅な変更は報告されていない。

#### 動作確認手順

1. macOS で `sdl_sample` を起動し、ウィンドウ表示・F キーフルスクリーン・Q キー終了を確認
2. macOS で `sdl_sample` の映像レンダリングに色味やアスペクト比の異常がないか目視確認
3. macOS で `sumomo --use-sdl` を起動し、ビデオ表示が正しく動作することを確認
4. Windows / Linux は CI（`build.yml` の sdl_sample / sumomo ビルドジョブ）でビルド確認。Linux 環境が利用可能であれば `sdl_sample` の起動確認も推奨（スレッド安全性のリスク検証のため）

### スコープ外

- SDL 2.x 対応
- SDL3 3.4.x の新機能をサンプルコードで利用する対応

## 完了条件

- `examples/DEPS` の `SDL3_VERSION` が `3.4.12` になっていること
- `buildbase.py` の `install_sdl3` 関数の Linux 用 CMake オプションに `-DSDL_X11_XTEST=OFF` が追加されていること
- macOS / Windows / Linux / Ubuntu ARMv8 の各プラットフォームでビルドが通ること
- `sdl_sample` が正しく動作すること
- `sumomo` の `--use-sdl` モードでビデオ表示が正しく動作すること
- `CHANGES.md` の `## develop` 配下の既存 SDL3 エントリ（存在する場合）を以下の内容で差し替えること（凡例順 CHANGE → ADD → UPDATE → FIX を尊重）。担当者ハンドル `@<担当者>` は PR 作成者のものに書き換える:

```
- [UPDATE] SDL3 を 3.4.12 にあげる
  - examples/DEPS の SDL3_VERSION を 3.4.12 に変更する
  - SDL 3.4.0 で追加された SDL_X11_XTEST オプション（デフォルト ON）が Linux ビルドでエラーになるため、buildbase.py に -DSDL_X11_XTEST=OFF を追加する
  - @<担当者>
```

## 解決方法

1. `examples/DEPS` の `SDL3_VERSION` を `3.2.28` → `3.4.12` に変更する
2. macOS でローカルビルドし、`sdl_sample` と `sumomo --use-sdl` が動作することを確認する
3. `buildbase.py` の `install_sdl3` 関数の Linux 用 CMake オプションに `-DSDL_X11_XTEST=OFF` を追加する（L1475 付近、`-DSDL_X11_XSYNC=OFF` の次）
4. CI で全プラットフォームのビルドが通ることを確認する
5. `CHANGES.md` の `## develop` 配下の既存 SDL3 エントリを差し替える。`[UPDATE] cmake のバージョンを 4.3.2 にあげる` の次に配置する
