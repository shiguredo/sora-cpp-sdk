# BaseRenderer の枠割りが回転映像のアスペクトを考慮しない

- Created: 2026-08-02
- Completed: 2026-08-04
- Branch: feature/fix-base-renderer-rotation-aspect
- Polished: 2026-08-04
- Reporter: @voluntas

## 目的

`BaseRenderer` の枠割りは入力映像のアスペクトを回転前の寸法から算出するため、90° / 270° 回転した映像では表示アスペクトと枠アスペクトが一致せず、枠内 letterbox が余分に広がる。また現行の回転処理は scaled 経路でのみ適用され、かつ回転後の寸法を考慮しないため、バッファ境界超過と描画崩れを起こす壊れた状態にある。回転後のアスペクトを考慮した枠割りに修正し、回転映像を正しく表示できるようにする。

## 現状

`Sink::OnFrame()` (src/renderer/base_renderer.cpp) の回転処理には次の問題がある。

- 回転の適用は scaled 経路でのみ行われており (`webrtc::I420Buffer::Rotate()`)、非 scaled 経路では回転メタデータが無視されて横向きのまま表示される
- 回転 90° / 270° では `Rotate()` 後のバッファの幅と高さが入れ替わるが、`ConvertFromI420()` のストライドと合成ループの寸法 (`GetFrameWidth()` / `GetFrameHeight()`) が回転前基準のままであり、scaled 経路ではバッファ境界超過と描画崩れを起こす

なおローカルで生成した映像の回転メタデータは `ScalableVideoTrackSource::OnCapturedFrame()` で正規化されて 0 に戻るため、非ゼロ回転がレンダラーに届くのはリモート受信経路のみである。

`BaseRenderer::SetOutlines()` の `frame_aspect` は `Sink::input_width_` / `Sink::input_height_` (回転前のフレームサイズ) から算出される。そのため 90° / 270° 回転映像では、実表示アスペクトと枠アスペクトが乖離し、代表 Sink の実測アスペクトを枠に採用する「代表 Sink が枠にぴったり収まる」前提が崩れ、枠内 letterbox が余分に広がる。

## 設計方針

- `Sink` に回転情報を保持するフィールドを追加し、`Sink::OnFrame()` で `frame.rotation()` を記録する
- 回転 0° 以外 (90° / 180° / 270°) の映像は scaled / 非 scaled の両経路で回転を適用して表示する (非 scaled 経路の回転無視も修正する。180° は寸法・アスペクトが変わらないため枠割りの再計算トリガーには含めない)
- 代表 Sink のアスペクト算出 (`BaseRenderer::SetOutlines()`) と per-cell letterbox 計算 (`Sink::OnFrame()`) は、保持した回転が 90° / 270° の場合に幅と高さを入れ替えた回転後寸法から算出する
- scaled 経路の縮小は、回転前の寸法 (回転後表示寸法の幅と高さを入れ替えた寸法) をターゲットに `ScaleFrom()` し、`Rotate()` 後に表示寸法と一致させる (非一様スケールによる歪みを防ぐ)
- 非 scaled 経路の表示寸法は回転後の入力寸法とし、`scaled_` の判定 (`width_ < input_width_`) は回転 90° / 270° の場合に回転後の入力寸法と比較する (回転後に枠より大きくなる映像が非 scaled 経路に入ってバッファ境界を超えないようにする)
- `ConvertFromI420()` のストライド、合成ループの寸法 (`GetFrameWidth()` / `GetFrameHeight()`) を回転後の表示寸法に整合させ、バッファ境界超過と描画崩れを起こさないようにする
- 回転の変化 (寸法が変わらない 0° → 90° など) は、枠割りの再計算 (dirty 検出) と `Sink::OnFrame()` の per-cell letterbox 再計算の両方のトリガーに含める (それぞれの条件に回転の比較を追加する)
- 既存の `Sink::OnFrame()` の分岐構造は維持しつつ、変更範囲を上記の整合に必要な範囲に限定する
- なお `Sink::OnFrame()` のロック位置を変更するデータレース修正 (0047)、`BaseRenderer::SetOutlines()` の枠割り計算を抽出するテスト追加 (0045)、`BaseRenderer::Sink` の未使用フィールドを削除するリファクタリング (0046) と変更箇所が重なるため、実装時に競合解消を行うこと
- 代表 Sink と他の Sink の回転状態が混在する場合のレイアウト調整は本 issue のスコープ外とする (枠は代表 Sink の回転後アスペクトに合わせ、他の Sink は既存の枠内フィットで表示する)

## 完了条件

- 90° / 270° 回転映像で、scaled / 非 scaled の両経路で枠割りと表示アスペクトが一致し、正しく表示されること
- 回転なし映像の挙動が変わらないこと
- ローカルビルド (`python3 run.py build --test --disable-cuda macos_arm64`) と既存テストが通ること
- `python3 run.py format` で clang-format に差分が出ないこと
- 変更履歴 (CHANGES.md) の develop にあるコア SDK の `[FIX]` 群の先頭に、本修正のエントリを追記すること
- 検証: 回転メタデータを制御できる手段が SDK に無い (付与するのは macOS のカメラキャプチャのみで回転値はデバイス依存) ため、開発用に一時的に回転を設定する検証コードを追加して SDL 表示で目視確認する (確認後に削除し、コミットに含めない)。fake_video_capturer 経路は `ScalableVideoTrackSource::OnCapturedFrame()` で回転が正規化されて 0 に戻るため、回転はレンダラーの `Sink::OnFrame()` に届く位置で設定すること。scaled 経路 (入力が枠より大きい構成) と非 scaled 経路 (入力が枠より小さい構成) の両方で確認すること

## 解決方法

`src/renderer/base_renderer.cpp` と `include/sora/renderer/base_renderer.h` を修正した。

- `BaseRenderer::Sink` に回転情報を保持する `rotation_` フィールドを追加し、`Sink::OnFrame()` で `frame.rotation()` を記録する (`IsRotated90Or270()` で 90° / 270° を判定)
- `Sink::OnFrame()` の per-cell letterbox 計算は、回転 90° / 270° の場合に幅と高さを入れ替えた回転後寸法からアスペクトを算出する
- 回転 0° 以外は scaled / 非 scaled の両経路で `webrtc::I420Buffer::Rotate()` により回転を適用する (非 scaled 経路の回転無視も修正)
- scaled 経路の縮小は、回転前の寸法 (回転後表示寸法の幅と高さを入れ替えた寸法) をターゲットに `ScaleFrom()` し、`Rotate()` 後に表示寸法と一致させる (非一様スケールによる歪みを防ぐ)
- `scaled_` の判定は回転 90° / 270° の場合に回転後の入力寸法と比較し、回転後に枠より大きくなる映像を縮小対象に含める
- `ConvertFromI420()` のストライドと合成ループの寸法 (`GetFrameWidth()` / `GetFrameHeight()`) を回転後の表示寸法に整合させる
- 回転状態の変化 (0° → 90° など) は、枠割りの再計算 (`input_size_dirty_`) と per-cell letterbox 再計算の両方のトリガーに含める。寸法・アスペクトが変わらない 180° 回転と 90° ↔ 270° の遷移はトリガーに含めない
- `BaseRenderer::SetOutlines()` の代表 Sink のアスペクト算出は、回転 90° / 270° の場合に幅と高さを入れ替えた回転後寸法から算出する

テストの追加は行っていない。完了条件が SDL 表示での目視確認を要求しており、`BaseRenderer::Sink` は private クラスで回転メタデータを制御する手段が SDK に無いためである。検証は、`Sink::OnFrame()` に届くフレームの回転を 90° に固定する一時的な検証コードを追加して sumomo の SDL 表示で行い、scaled 経路 (入力 640x480 より小さいウィンドウ) と非 scaled 経路 (入力より大きいウィンドウ) の両方で、縦横比が保たれた回転表示と枠内への収まりを確認した (確認後に検証コードは削除済み)。270° は 90° と同一のコードパスを通るため、90° の確認をもって検証済みとした。

ローカルビルド (`python3 run.py build --test --disable-cuda macos_arm64`) とローカルで実行可能な既存テスト (`video_factory_data_race` / `audio_device`) が通ることを確認した。

`CHANGES.md` の develop にあるコア SDK の `[FIX]` 群の先頭にエントリを追記した。
