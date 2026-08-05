# BaseRenderer がフルスクリーン時に映像を枠の寸法まで拡大しない

- Created: 2026-08-05
- Completed: {YYYY-MM-DD}
- Branch: feature/fix-base-renderer-upscale-video-to-fill-outline
- Polished: {YYYY-MM-DD}
- Reporter: @voluntas

## 目的

`BaseRenderer` は複数の映像トラックを含む描画時、ウィンドウを `cols × rows` の枠 (outline) に分割し、各枠に映像を配置している。
各 Sink は枠の中で映像アスペクトを維持してフィットサイズを計算するが、**枠が入力映像より大きい場合にネイティブサイズのまま描画し、枠いっぱいに拡大しない**。

その結果、フルスクリーン時など枠が大きくなるケースで各枠内に大きな黒帯が生じ、複数映像を受信すると映像同士が離れて見える。本 issue ではこの問題を修正する。

## 現状

`src/renderer/base_renderer.cpp` の `BaseRenderer::Sink::OnFrame()` は、枠に合わせたフィットサイズ (`width_` / `height_`) を常に計算しているが、`scaled_` の判定が縮小のみに限定されている。

```cpp
// scaled_ の判定は回転後の表示寸法と枠の寸法を比較する。
int display_width = rotated ? input_height_ : input_width_;
scaled_ = width_ < display_width;
if (scaled_) {
  image_.reset(new uint8_t[width_ * height_ * 4]);
} else {
  image_.reset(new uint8_t[input_width_ * input_height_ * 4]);
}
```

`scaled_ = false` (枠が入力映像より大きい) の場合、`GetFrameWidth()` / `GetFrameHeight()` が入力サイズを返し、映像はネイティブサイズのまま枠内にセンタリングされる。

### 再現条件

sumomo をローカルの SDK 実装と紐付けてビルドしたうえで、同じ `channel_id` に対して 2 つのプロセスを起動して sinks=2 を作る。

プロセス 1 (観測対象、双方向で自映像とリモート映像の 2 つを持たせる):

```text
--use-sdl
--show-me
--fake-capture-device
--resolution FHD
--fullscreen
--role sendrecv
--video-codec-type VP8
--signaling-url <URL>
--channel-id <ID>
```

プロセス 2 (相手側、同じ `channel_id` で送信のみでよい):

```text
--fake-capture-device
--resolution FHD
--role sendonly
--video-codec-type VP8
--signaling-url <URL>
--channel-id <ID>
```

### 具体例: フルスクリーン 5120x2160 (21:9) + FHD 映像 + sinks=2 の場合

`SetOutlines()` は `window_aspect (2.37) >= frame_aspect (16:9)` の分岐で cols=2 / rows=1 を選び、各枠は `Sink::SetOutlineRect()` で 2560x1440 (y オフセット 360) に確定する。枠同士は隣接し、上下に 360 px ずつの外周帯ができる。ここまでは正しい。

問題は `Sink::OnFrame()` の描画段階で、実測された各 Sink の描画領域は次の通り:

- ローカル映像 (FHD 1920x1080): 枠 2560x1440 の中でネイティブサイズのまま描画され、左右に 320 px ずつの黒帯が生じる
- リモート映像 (帯域制限により 1280x720 で届く): 枠 2560x1440 の中でネイティブサイズのまま描画され、左右に 640 px ずつの黒帯が生じる

結果、2 つの映像の間に約 960〜1280 px の黒帯が空き、「映像が離れて見える」状態になる。ウィンドウサイズ時は枠 (例: 640x360) が入力映像より小さいため downscale 経路を通り、映像が枠を埋めるため問題が顕在化しない。

なお、枠割り (SetOutlines) 側の映像アスペクト考慮は既に修正済み (closed の 0044) であり、本 issue は枠内のスケール処理 (`Sink::OnFrame()`) が対象の別問題である。

## 設計方針

`Sink::OnFrame()` で映像を枠の寸法に合わせて**常に拡大縮小**する。フィットサイズ (`width_` / `height_`) は既に枠内に映像アスペクトを保って収まるよう計算されているため、拡大して枠を埋めることで黒帯が消え、映像同士が隣接する。

- 縮小のみだった `scaled_` 判定を廃止し、常にフィットサイズへスケールする
- 不要になる非スケール経路 (回転のみ適用する分岐) と `scaled_` メンバを削除する
- 映像アスペクトと枠アスペクトが異なる場合の letterbox (枠内センタリング) は従来どおり維持する

## 完了条件

- フルスクリーン時 (枠が入力映像より大きい場合) に、映像が枠いっぱいに拡大されて描画されること
- 複数映像受信時に映像同士が隣接し、枠内の黒帯による隙間が生じないこと
- ウィンドウサイズ時 (downscale が必要な場合) の表示が従来どおりであること
- 回転映像 (90° / 270°) の表示が従来どおりであること
- `test/` に `BaseRenderer::Sink::OnFrame()` のスケール経路のユニットテストが追加され、通過すること

## 解決方法

`src/renderer/base_renderer.cpp` の `BaseRenderer::Sink::OnFrame()` を以下のように変更する。

- `scaled_` 判定を削除し、`image_` を常にフィットサイズ (`width_ * height_ * 4`) で確保する
- スケール経路を常に実行し、`I420Buffer::Create` + `ScaleFrom()` で枠の寸法へ拡大縮小する
- 非スケール経路の分岐 (`buffer_if` の else) を削除する
- `GetFrameWidth()` / `GetFrameHeight()` を常に `width_` / `height_` を返すように簡素化する
- 不要になる `scaled_` メンバを `include/sora/renderer/base_renderer.h` から削除する
- 極小の枠でフィット寸法の片方が 0 になる場合に `I420Buffer::Create` の寸法チェックで abort しないよう、スケール対象の生成前に打ち切るガードを追加する

テストは次の 2 つで行う。

- `test/base_renderer.cpp` のユニットテスト (Catch2)。実フレームを生成するテスト用映像ソースと実トラックで `BaseRenderer::Sink::OnFrame()` のスケール経路を検証し、拡大・縮小・letterbox・回転 90°・ゼロ寸法ガードの各ケースを `Render()` に渡される `SinkInfo` とキャンバスのピクセルで確認する。`test/CMakeLists.txt` の `TEST_BASE_RENDERER` ターゲットとして追加し、`run.py` の `--run-e2e-test` 実行対象に含める
- sumomo の 2 プロセス構成での目視確認。確認項目は完了条件の各項目に加え、F キーによるフルスクリーン切替の往復で映像配置が破綻しないことを含める
