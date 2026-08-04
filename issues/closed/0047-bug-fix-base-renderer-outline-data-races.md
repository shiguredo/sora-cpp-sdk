# BaseRenderer の枠情報のデータレースを修正する

- Created: 2026-08-02
- Completed: 2026-08-04
- Branch: feature/fix-base-renderer-outline-data-races
- Polished: 2026-08-04
- Reporter: @voluntas

## 目的

`BaseRenderer::Sink` の枠情報 (outline) の一部が `frame_params_lock_` 保護外で読み書きされている。`Sink::OnFrame()` (ビデオスレッド) 冒頭の早期リターン条件はロックなしで `outline_width_` / `outline_height_` を読み、`Sink::SetOutlineRect()` (描画スレッド・イベント処理スレッド等の複数スレッド) の書き込みと同期されておらずデータレースになる。`Sink::SetOutlineRect()` 冒頭の `outline_offset_x_` / `outline_offset_y_` 書き込みも `frame_params_lock_` 保護外だが、呼び出し元が常に `sinks_lock_` を保持するため実レースにはならず、暗黙の直列化に依存している。ロック保護の範囲を正して、データレースと暗黙依存を排除する。

## 現状

次の 2 箇所に `frame_params_lock_` 保護外の読み書きがある (src/renderer/base_renderer.cpp)。

- `Sink::OnFrame()` 冒頭の早期リターン条件: `outline_width_ == 0 || outline_height_ == 0` をロック取得前に読んでいる。この読みは `Sink::SetOutlineRect()` の書き込みと同期されておらず、データレースになっている
- `Sink::SetOutlineRect()` 冒頭: `outline_offset_x_` / `outline_offset_y_` の書き込みと early return 判定の `outline_width_` / `outline_height_` 読みを `frame_params_lock_` 取得前に行っている。この経路は呼び出し元 (`SetOutlines()` 経由の `SetSize()` / `AddTrack()` / `RemoveTrack()` / `RenderThread()`) が常に `sinks_lock_` を保持しているため実レースにはなっていないが、暗黙の直列化に依存した状態である

読み取り側の `BaseRenderer::RenderThread()` の合成ループは、`sinks_lock_ → frame_params_lock_` の順でロックを取得して `GetOffsetX()` / `GetOffsetY()` を読む。

なお `examples/sdl_sample/src/sdl_renderer.cpp` の独自 Sink にも同型の保護外アクセスがあるが、sdl_sample は BaseRenderer への統合 (別 issue で対応予定) で解消されるため、本 issue の対象外とする。

## 設計方針

- `Sink::OnFrame()` はロック取得を 1 段目の早期リターン (outline 未確定チェック) より前に移動し、`outline_width_` / `outline_height_` の読みを `frame_params_lock_` 保護下に収める (2 段目の `frame.width() == 0` チェックはフレーム引数のローカル値だけを見るため移動不要)
- `Sink::SetOutlineRect()` はロック取得を冒頭へ移動し、`outline_offset_x_` / `outline_offset_y_` の書き込みと early return 判定を `frame_params_lock_` 保護下に収め、暗黙依存を明示的なロック保護に変える
- 既存のロック順序 (`sinks_lock_ → frame_params_lock_`) を維持する
- `SetOutlineRect()` の early return の挙動は変えない
- 定常状態では `Sink::OnFrame()` は既に毎フレーム `frame_params_lock_` を取得しているため、取得位置を移動してもロック取得回数は増えない
- なお `Sink::OnFrame()` の letterbox 計算を変更する回転アスペクト対応の別 issue (0049) と変更箇所が重なるため、実装時に競合解消を行うこと

## 完了条件

- 枠情報の読み書きがすべて `frame_params_lock_` 保護下になり、`Sink::OnFrame()` のロックなし読みと `Sink::SetOutlineRect()` の保護外読み書きが解消され、既存のロック順序 (`sinks_lock_ → frame_params_lock_`) が維持されていること (コードレビューで確認できること)
- ローカルビルド (`python3 run.py build --test --disable-cuda macos_arm64`) と既存テストが通ること
- `python3 run.py format` で clang-format に差分が出ないこと
- 変更履歴 (CHANGES.md) の develop にあるコア SDK の `[FIX]` 群の先頭に、本修正のエントリを追記すること

## 解決方法

`src/renderer/base_renderer.cpp` の `Sink::OnFrame()` と `Sink::SetOutlineRect()` のロック取得位置を冒頭へ移動した。

- `Sink::OnFrame()`: 枠未確定チェック (`outline_width_ == 0 || outline_height_ == 0`) を `frame_params_lock_` 取得後に移動し、ロックなし読みを解消した。2 段目の `frame.width() == 0` チェックはフレーム引数のローカル値だけを見るため移動していない
- `Sink::SetOutlineRect()`: `outline_offset_x_` / `outline_offset_y_` の書き込みと early return 判定を `frame_params_lock_` 取得後に移動し、`sinks_lock_` への暗黙の直列化依存を明示的なロック保護に変えた
- 既存のロック順序 (`sinks_lock_ → frame_params_lock_`) は維持したまま、ロック取得位置の移動のみで挙動を変えていない

テストの追加は行っていない。完了条件がコードレビューで確認できることを要求しており、メモリレベルのデータレースはサニタイザオプションの無いこのビルド基盤では決定論的なテストにできないためである。ローカルビルド (`python3 run.py build --test --disable-cuda macos_arm64`) とローカルで実行可能な既存テスト (`video_factory_data_race` / `audio_device`) が通ることを確認した。

`CHANGES.md` の develop にあるコア SDK の `[FIX]` 群の先頭にエントリを追記した。
