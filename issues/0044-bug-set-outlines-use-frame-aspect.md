# BaseRenderer の枠割りが映像アスペクトを無視して映像間に黒帯を広げる

- Priority: Medium
- Created: 2026-07-29
- Completed: {YYYY-MM-DD}
- Model: Opus 4.7
- Branch: feature/fix-set-outlines-use-frame-aspect
- Polished: 2026-07-30

## 目的

`BaseRenderer` は複数の映像トラックを含む描画時、ウィンドウを `cols × rows` の枠 (outline) に分割し、各枠に映像を配置している。
現行の `BaseRenderer::SetOutlines()` は、この枠割りと枠サイズを **ウィンドウのアスペクト比のみ** から決めており、実際の入力映像のアスペクトを一切考慮していない。
その結果、フルスクリーン時などウィンドウのアスペクトと映像のアスペクトが乖離するケースで、各枠内で映像が大きく letterbox され、映像同士の間・映像とウィンドウ端の間に大きな黒帯が広がる。

本 issue では、`BaseRenderer::SetOutlines()` を次の 3 方針で修正する。

- 方針 A: `frame_aspect` を Sink の入力映像アスペクト実測値から決定し、cols/rows 選択の分岐を映像アスペクトに沿わせる
- 方針 B: cols/rows 決定後の各枠を映像アスペクトに合わせて共通縮小し、ウィンドウ中央寄せで配置する
- 方針 C: 方針 A / B が入力映像サイズの確定・変化に追従できるよう、`Sink::OnFrame()` と `BaseRenderer::RenderThread()` の間を dirty フラグで結ぶ再計算経路を新設する

方針 A + B は `BaseRenderer::SetOutlines()` の枠割りロジックを 2 段構成で置き換える主対策。方針 C はそれを支える経路変更で、`Sink` 側にフィールドと `Sink::OnFrame()` 側の判定を追加し、`BaseRenderer::RenderThread()` の毎イテレーション先頭に dirty 検出フェーズを差し込む。

## 優先度根拠

- クラッシュや異常終了は発生せず、機能上の致命的な影響はない
- ただし `--fullscreen` などウィンドウが映像アスペクトから離れた状態で、ユーザーの体感上、映像が「離れて見える」ため視認性が明確に損なわれる
- 直近 PR #357 (`70df13e3`) で描画バッファ境界超過を修正した結果、フルスクリーン描画が回るようになって顕在化した現象

## 現状

`src/renderer/base_renderer.cpp` の `sora::STD_ASPECT` / `sora::WIDE_ASPECT` 定数と `BaseRenderer::SetOutlines()` は次の 2 択ヒューリスティックで `frame_aspect` を決めている。

```cpp
static constexpr float STD_ASPECT = 1.33f;   // 4:3
static constexpr float WIDE_ASPECT = 1.78f;  // 16:9

void BaseRenderer::SetOutlines() {
  float window_aspect = (float)width_ / (float)height_;
  bool window_is_wide = window_aspect > ((STD_ASPECT + WIDE_ASPECT) / 2.0);
  float frame_aspect = window_is_wide ? WIDE_ASPECT : STD_ASPECT;
  int rows = 1;
  int cols = 1;
  if (window_aspect >= frame_aspect) {
    int times = std::floor(window_aspect / frame_aspect);
    if (times < 1) times = 1;
    while (rows * cols < sinks_.size()) {
      if (times < (cols / rows)) { rows++; } else { cols++; }
    }
  } else {
    int times = std::floor(frame_aspect / window_aspect);
    if (times < 1) times = 1;
    while (rows * cols < sinks_.size()) {
      if (times < (rows / cols)) { cols++; } else { rows++; }
    }
  }
  int outline_width = std::floor(width_ / cols);
  int outline_height = std::floor(height_ / rows);
  // cols × rows で等分割された矩形をそのまま各 Sink に割り当てる
  ...
}
```

`frame_aspect` は `STD_ASPECT (1.33)` と `WIDE_ASPECT (1.78)` の 2 値からしか選ばれず、実際の入力映像アスペクトとの整合はまったく取られていない。
また cols/rows 決定後、各 Sink の枠は `BaseRenderer::SetOutlines()` 内でウィンドウ全域を等分割した矩形として計算され、`Sink::SetOutlineRect()` に渡される。
各 Sink は割り当てられた枠の中で映像アスペクトを維持してセンタリングされる (`Sink::OnFrame()` の `frame_aspect > outline_aspect_` 分岐で幅基準か高さ基準かを選ぶ) ため、枠のアスペクトと映像アスペクトが乖離すると、そのぶん枠内で黒帯が広がる。

### 再現条件

sumomo をローカルの SDK 実装と紐付けてビルドしたうえで、**同じ `channel_id` に対して 2 つのプロセスを起動して sinks=2 を作る**。`examples/sumomo/src/sumomo.cpp` の `Sumomo::Run()` にある `--show-me` によるローカル映像追加経路 (`sdl_renderer_->AddTrack(video_track_.get())`) と `Sumomo::OnTrack()` によるリモート映像追加経路 (`sdl_renderer_->AddTrack(...)`) の 2 箇所しか SDL レンダラーへのトラック追加はなく、単一プロセスで sinks=2 にするには `sendrecv` role でリモートを受信する必要がある。両プロセスに `--resolution FHD` を明示指定し、fake-capture-device に FHD (1920×1080, 16:9) の映像を生成させる (`SumomoConfig::resolution` に応じた `SumomoConfig::GetSize()` の分岐で `FHD` → `{1920, 1080}` が返る)。方針 A + B のピクセル配置は `Sink::OnFrame()` の downscale 経路 (`scaled_ = width_ < input_width_`) を通ることを前提とする数値予測になっており、代表 Sink の入力サイズがウィンドウ側で決まる cell 幅より大きい構成が必要なため FHD 以上を使う (`--resolution` のデフォルトである VGA 640×480 では downscale 経路を通らず、本 issue の数値予測が成立しない)。

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

プロセス 1 が接続確立後、自映像 (`--show-me` 経由) + プロセス 2 からの受信映像 (`Sumomo::OnTrack()` 経由) の合計 2 sinks が SDL 側の `BaseRenderer` に登録される。
プロセス 1 で通常サイズ起動後に F キーでフルスクリーンに切り替えても同様の状態が観測できる。

### 具体例: フルスクリーン 2560×1440 (16:9) + FHD (16:9) 映像 + sinks=2 の場合

`window_aspect = (float)2560 / (float)1440` は 16/9 の float32 で約 `1.77777779`。
`WIDE_ASPECT = 1.78f` は真値 1.78 の直近 float32 で約 `1.77999997` (16/9 より僅かに大きい)。
したがって `window_aspect >= frame_aspect` は false になり、else 分岐 (`window_aspect < frame_aspect`) に入る。

- `times = std::floor(frame_aspect / window_aspect) = std::floor(1.78 / 1.7778) = 1`
- while 初回: `rows*cols == 1 < 2`、`times < (rows/cols) == 1 < 1 == false` → `rows++`
- 結果: **cols=1, rows=2** → 各枠は **2560×720 (アスペクト 3.56 の横長)**

そこに FHD (16:9, `frame_aspect = 1920/1080 ≈ 1.7778`) の映像を入れると、`Sink::OnFrame()` は枠高さを基準に映像を縮める分岐に入る。

- `frame_aspect (1.7778) > outline_aspect (3.56)` は false → else 分岐 (`height = outline_height`)
- `height = 720`, `width = 720 * 1.7778 = 1280`, `offset_x = (2560 - 1280) / 2 = 640`
- `scaled_ = width_ (1280) < input_width_ (1920)` は true → downscale して 1280×720 として合成される

結果、各枠 2560×720 の中央に downscale した 1280×720 の映像が配置され、**各 sink の左右にそれぞれ 640 px の黒帯**が生じる。sinks は縦に 2 段並び上下方向の cell 間ギャップは 0 だが、左右方向にウィンドウ全幅の 50% (1,843,200 px、両 sink 分の合計) が黒領域になる。

### 本 issue のスコープ外

- 映像アスペクトが sink ごとに大きく異なる場合の代表 Sink 選定の UX 検討 (方針 A は「走査時点で登録順の先頭に近い有効 Sink」を採用するのみ)
- 方針 A の代表 Sink 選定 (方針 A で定義。走査ごとに毎回選び直すため、後方 Sink が先に入力を受け取ったあと後から先頭側 Sink が初回入力を受信した場合に代表 Sink が入れ替わる) に伴う `AddTrack()` 順序への強い依存性 (`RemoveTrack()` は先頭側 Sink が消えた際に同種の切替を起こす)
- `BaseRenderer::SetOutlines()` 以外の描画・スケール処理
- 既存の `Sink` コンストラクタで初期化されていない `outline_aspect_` / `offset_x_` / `offset_y_` などのフィールド初期化整理 (本 issue の実装で追加する `input_size_dirty_` は初期化する。既存の未初期化フィールドの整理は本 issue の完了条件に含めず、別 issue として担当者が別途起票する)
- `Sink::OnFrame()` 冒頭の `if (outline_width_ == 0 || outline_height_ == 0) return;` (ロック取得前に `outline_width_` を読む pre-existing なデータレース) と、`Sink::SetOutlineRect()` 冒頭で `outline_offset_x_` / `outline_offset_y_` を `frame_params_lock_` 保護外で書き換える pre-existing race。整理は別 issue として担当者が別途起票する (方針 A/B/C がこれらの race の危険度を上げない根拠は「方針 B」節に記載)

## 設計方針

問題は 2 つの層に分かれる。

- **層 A**: cols/rows 選択が映像アスペクトを無視している (現行 `BaseRenderer::SetOutlines()` の `frame_aspect` 2 択ヒューリスティック)
- **層 B**: cols/rows 決定後の各枠がウィンドウ全域を等分割するため、枠アスペクトと映像アスペクトが乖離した場合に枠内 letterbox が生じる

本 issue は方針 A (層 A への対策) と方針 B (層 B への対策) を両方入れ、方針 C でその適用タイミングを実映像入力に追従させる。

### 方針 A: frame_aspect を映像アスペクトの実測値から決定する

`BaseRenderer::SetOutlines()` の `frame_aspect` を、Sink 群の `Sink::input_width_` / `Sink::input_height_` から算出した実測アスペクトに置き換える。

- `sinks_` を `AddTrack()` 登録順で先頭から走査し、`Sink::input_width_` / `Sink::input_height_` が両方とも非 0 の最初の Sink (**以降「代表 Sink」と呼ぶ**) のアスペクト (`(float)input_width_ / (float)input_height_`) を `frame_aspect` として採用する。代表 Sink を見つけた時点でループを抜け、それ以降の Sink の `Sink::frame_params_lock_` は取得しない
- 走査時、各 Sink の `Sink::input_width_` / `Sink::input_height_` は `Sink::frame_params_lock_` 保護下のメンバであるため (`Sink::OnFrame()` 内で `webrtc::MutexLock lock(GetMutex());` を取得してから書き換える箇所と同じロック)、走査する各 Sink について `Sink::frame_params_lock_` を 1 件ずつ取得してローカルにコピーしてから解放する。`BaseRenderer::SetOutlines()` は既に `BaseRenderer::sinks_lock_` 保持中で呼ばれるため、ロック順序は `sinks_lock_ → frame_params_lock_` の既存規約 (`BaseRenderer::RenderThread()` の合成ループが取っている順) と一致する
- どの Sink も入力サイズを持たない初期状態および `sinks_` が空の初期状態では、既存の `STD_ASPECT` / `WIDE_ASPECT` の 2 択ヒューリスティックにフォールバックする
- cols/rows を決定する `while` ループの条件式は既存を維持する

代表 Sink は走査ごとに再選定されるため、時間経過で先頭側の Sink が初回入力を受信すると代表 Sink が入れ替わることがある。方針 C の再計算経路で毎回走査し直すこの動的挙動は許容し、代表 Sink を固定化するキャッシュは持たない。

これにより再現条件 (FHD 16:9 + 2560×1440 window) では、代表 Sink のアスペクトが `window_aspect` と等しく (両者とも 16/9 の float32 で約 1.77777779) `window_aspect >= frame_aspect` が true となり **if 分岐** に入る。`times = std::floor(window_aspect / frame_aspect) = 1`、while 初回で `times < (cols/rows) == 1 < 1 == false` → `cols++` で **cols=2, rows=1** に切り替わる。各枠は 1280×1440、`Sink::OnFrame()` の枠幅基準分岐で映像は width=1280, height=int(1280/(16/9))=720 に letterbox され、offset_y=(1440-720)/2=360 で downscale (`scaled_ = 1280 < 1920 = true`) して配置される。黒帯の総面積 (1,843,200 px) は修正前後で変わらないが、**修正前は各 sink の左右 640 px 帯 (映像同士の並びと直交する側) として現れていた黒帯が、修正後はウィンドウ上下に 360 px ずつの外周帯へ集約される**。cell 間の隙間が消え、映像同士がぴったり並ぶ視認性の改善が得られる。

このケースは cell の並び方向と映像アスペクト (16:9) が整合しており、方針 B の共通縮小と `Sink::OnFrame()` の per-cell letterbox は **float 丸めによる 1〜2 px 以内の差を除いてほぼ同一のピクセル位置**を生む (方針 B が追加の視覚差を実質生まないケース)。

方針 A の効果は、最初の `AddTrack()` からの `BaseRenderer::SetOutlines()` 呼び出し時点で対象 Sink がまだ入力を受け取っておらず (`Sink::input_width_ / input_height_` が 0)、代表 Sink 候補が全滅している場合にはフォールバックが走る。その Sink の初回 `Sink::OnFrame()` で入力サイズが確定した後、方針 C の再計算経路を経て `BaseRenderer::SetOutlines()` が呼び直されて cols/rows が確定する。既に入力受信済みの sink がある状態で後続の `AddTrack()` が呼ばれた場合は、その `AddTrack()` 内の `SetOutlines()` の時点で代表 Sink (先頭の非 0 sink) の実測アスペクトが採用されるため、フォールバック期間は生じない。この順序依存性は方針 C 側で担保する。

### 方針 B: 枠を映像アスペクトに合わせて共通縮小しウィンドウ中央寄せする

方針 A で cols/rows が改善しても、`sinks_.size() == 2` のように while ループが単一結果に確定するケースでは、cell の並び方向に沿った側 (縦積み cols=1/rows=2 なら映像の上下、横並び cols=2/rows=1 なら映像の左右) で cell 間に映像アスペクト由来の黒帯が生じる。とくに以下 2 パターンで cell 間の黒帯として視覚化される。

- 縦長ウィンドウ + 横長映像 + cols=1, rows=2 → 各 cell 内で上下 letterbox → cell と cell の境界近傍が黒
- 横長ウィンドウ + 縦長映像 + cols=2, rows=1 → 各 cell 内で左右 letterbox → cell と cell の境界近傍が黒

方針 B は cell 内 letterbox の分を cell 側から取り除き、まとめてウィンドウ外周に集約する。cell の並び方向と映像アスペクトが整合する方向 (cols=2, rows=1 で横長映像、cols=1, rows=2 で縦長映像) では、既存の `Sink::OnFrame()` の per-cell センタリングと **float 丸めによる 1〜2 px 以内の差を除いてほぼ同一のピクセル位置**を生み、方針 B は視覚的な差を実質生まない (実装は同じ経路で通す)。

- cols/rows 決定後、`content_aspect = frame_aspect` (方針 A で決めた値、フォールバック時は `STD_ASPECT` / `WIDE_ASPECT` の 2 択) を用いる
- `raw_outline_width_f = (float)width_ / cols`、`raw_outline_height_f = (float)height_ / rows` から `raw_outline_aspect = raw_outline_width_f / raw_outline_height_f` を算出する (すべて float 変数)
- `content_aspect > raw_outline_aspect` (映像の方が横長) なら枠幅基準:
  - `ideal_outline_width_f = raw_outline_width_f`
  - `ideal_outline_height_f = ideal_outline_width_f / content_aspect`
- `content_aspect <= raw_outline_aspect` (映像の方が縦長または一致) なら枠高さ基準:
  - `ideal_outline_height_f = raw_outline_height_f`
  - `ideal_outline_width_f = ideal_outline_height_f * content_aspect`
- 全枠共通の grid オフセットを算出する (float)。IEEE 754 float32 では `(float)width_ / cols * cols > (float)width_` が起こりうるため、`std::max(0.0f, ...)` で **必ず非負にクランプする** (これを怠ると累積式の `x_0 = std::floor(grid_offset_x_f)` が `-1` を返して `Sink::SetOutlineRect()` に負のオフセットが渡り、`BaseRenderer::RenderThread()` の合成ループが `image.data() + sink->GetOffsetX() * 4 + ...` でバッファ手前へ書き出す描画バッファアンダーランを起こす):
  - `grid_offset_x_f = std::max(0.0f, ((float)width_ - ideal_outline_width_f * cols) / 2.0f)`
  - `grid_offset_y_f = std::max(0.0f, ((float)height_ - ideal_outline_height_f * rows) / 2.0f)`
- `Sink::SetOutlineRect(x, y, width, height)` に渡す各値は、隣接 cell 間の隙間を必ず 0 にするため、位置と幅を累積 (running sum) で int に丸めて算出する。累積式の帰結として左端 `std::floor(grid_offset_x_f)` と右端 `std::floor(cols * ideal_outline_width_f + grid_offset_x_f)` は最大 1 px の非対称に収まる (実運用上の視覚的対称性は保たれる):
  - `x_i = std::floor(i * ideal_outline_width_f + grid_offset_x_f)` (i=0..cols)
  - `width_i = x_{i+1} - x_i` (i=0..cols-1)
  - `y_j = std::floor(j * ideal_outline_height_f + grid_offset_y_f)` (j=0..rows)
  - `height_j = y_{j+1} - y_j` (j=0..rows-1)
- 上記の累積式に加えて、右端 `x_cols` / 下端 `y_rows` が上記 float 誤差の影響で `width_` / `height_` を超えた場合に備え、各累積値は `std::min((float)width_, ...)` / `std::min((float)height_, ...)` でも上側クランプしてから `std::floor` する (右辺クランプは描画バッファ境界超過を防ぐための保険で、通常経路では発動しない)
- 既存の `Sink::SetOutlineRect()` の early return (「`outline_width` と `outline_height` が同一なら `outline_offset_x_` / `outline_offset_y_` だけ更新して早期 return」) はそのまま維持する。方針 B で grid_offset が変わって枠の位置は動いたが幅高が丸め結果として同じだった場合、`Sink::OnFrame()` 内の per-cell センタリング (`offset_x_` / `offset_y_`) は `outline_width_` / `outline_height_` と frame の幅高だけで決まるため、outline サイズが同一なら旧センタリング値のまま `GetOffsetX() = 新 outline_offset_x_ + 旧 offset_x_` が新座標を返し、描画位置は正しく反映される。なお `Sink::SetOutlineRect()` 冒頭で `outline_offset_x_` / `outline_offset_y_` を `frame_params_lock_` 保護外で書き換える pre-existing race は本 issue のスコープ外 (「本 issue のスコープ外」節参照)。方針 B は `SetOutlineRect()` 呼び出し頻度を増やすが、`BaseRenderer::sinks_lock_` 保持下で `SetOutlineRect()` を呼ぶ経路 (`AddTrack()` / `RemoveTrack()` / `SetSize()` / 方針 C の再計算) は既存経路と同じスレッド安全性クラスに収まり、既存 race の危険度を上げない

これにより cell の並び方向に沿った側の余白がウィンドウ外周に集約される。代表 Sink の映像は縮小後の枠にほぼぴったり収まり (代表 Sink のアスペクトが content_aspect と一致するため `Sink::OnFrame()` の枠内 letterbox は 1〜2 px の丸め差程度)、代表以外の Sink はアスペクトが異なる可能性があり `Sink::OnFrame()` の既存フィット計算により従来通り枠内でセンタリングされる。
本 issue は変更範囲最小化のため `Sink::OnFrame()` の per-cell letterbox 計算分岐には手を入れず (方針 C で `input_size_dirty_` セットの 1 行のみ追加する)、`BaseRenderer::SetOutlines()` で共通縮小した枠と既存の `Sink::SetOutlineRect()` の組み合わせで方針 B の効果を得る。

フォールバック時 (方針 A の実測ができない状態) は方針 A の走査で「どの Sink も入力サイズを持たない」条件が成立している期間であり、`BaseRenderer::RenderThread()` の合成ループも各 Sink で入力サイズ未確定のため `Sink::GetImage()` に画像がなく、`memset` でクリアされた黒キャンバスがそのまま `Render()` へ渡される。この期間は方針 B の共通縮小結果を視覚化するピクセルが存在しないため、方針 B を無条件適用しても外周黒帯が「悪化」する視覚影響はない。分岐削減のため常時適用する。

`Render()` のシグネチャや SDL / ANSI / Sixel の各レンダラーの実装には手を入れない。

### 方針 C: 再計算経路とロック順序 (dirty フラグ方式)

方針 A の `frame_aspect` は入力映像のサイズが確定・変化したタイミングで再計算する必要があるが、`Sink::OnFrame()` は `Sink::frame_params_lock_` を保持中であり、そこから `BaseRenderer::sinks_lock_` を取得すると既存の取得順序 (`BaseRenderer::RenderThread()` の合成ループが `sinks_lock_ → frame_params_lock_` の順で取っており、PR #357 の描画バッファ修正でこの順序の適用範囲が合成前準備まで拡張された) と逆転してデッドロックする。
安全に再計算するため、`Sink::OnFrame()` からは `BaseRenderer::SetOutlines()` を直接呼ばず、dirty フラグ方式で `BaseRenderer::RenderThread()` に処理を移す。

**Sink 側の変更**:

- `Sink` に `bool input_size_dirty_` フィールドを追加し、コンストラクタ初期化リストで `false` に初期化する (`include/sora/renderer/base_renderer.h` の `Sink` クラス宣言と `src/renderer/base_renderer.cpp` の `Sink::Sink()` 初期化リスト両方に追加)
- `Sink::OnFrame()` に dirty セット文を **1 文** 追加する。追加位置は `webrtc::MutexLock lock(GetMutex());` の直後、既存の `if (outline_changed_ || frame.width() != input_width_ || frame.height() != input_height_)` 分岐の **外側 (前)** とする。追加文は次の 1 文のみ (clang-format の設定次第で 1 行または複数行に折られる可能性があるが、意味は同じ):
  - `if (frame.width() != input_width_ || frame.height() != input_height_) input_size_dirty_ = true;`
- 既存の `Sink::OnFrame()` 冒頭にはロック取得前の 2 段早期リターン (`outline_width_ == 0 || outline_height_ == 0` と `frame.width() == 0 || frame.height() == 0`) があり、この期間は追加した dirty セット文に到達しない。`AddTrack()` 内の `SetOutlines() → SetOutlineRect()` が完了して `outline_width_ / outline_height_` が非 0 になった後の最初のフレームで dirty が立つ (`AddOrUpdateSink()` によるフレーム配信は `AddTrack()` 呼び出し途中で開始されるため、最初期のフレームは 1 段目の早期リターンで捨てられる)。実運用では方針 C の再計算経路への影響はない (数フレーム以内に必ず dirty がセットされる)
- 判定式は既存の外側 `if` から独立して評価するため、`outline_changed_` を含めない (含めると `outline_changed_` 起因の再スケール時にも dirty が立ち、後述の無限ループ回避条件が壊れる)。既存の外側 `if` 分岐と、その内部の `input_width_ = frame.width(); input_height_ = frame.height();` の書き換えには手を加えない
- 追加した dirty セット文は既存の `Sink::frame_params_lock_` 保護下 (直前の `webrtc::MutexLock lock(GetMutex());` の直後) で実行される

**RenderThread 側の変更**:

`BaseRenderer::RenderThread()` の各イテレーションで、次のフローを取る。

1. `BaseRenderer::sinks_lock_` を取得する (既存動作を維持)
2. dirty 検出フェーズ: `sinks_` を先頭から走査し、各 Sink について `Sink::frame_params_lock_` を **1 件ずつ順次取得 → `input_size_dirty_` を確認 → true なら false にリセット → 解放** を繰り返す。この時点でどの Sink の `Sink::frame_params_lock_` も保持していない状態を作る (dirty フラグを 1 件でも見つけた事実は、走査中にローカル変数へ保持する)
3. dirty が 1 つでもあった場合、`BaseRenderer::sinks_lock_` を保持したまま `BaseRenderer::SetOutlines()` を呼び直す (`BaseRenderer::SetOutlines()` 内部で方針 A の走査に伴い `Sink::frame_params_lock_` を再取得するが、既に解放済みなので自己ロックは発生しない。`Sink::SetOutlineRect()` 内での Sink ロック取得も同様)
4. 既存の描画準備 (キャンバス寸法スナップショット・image resize・memset) と各 Sink 合成 (compose ループ、`for (const VideoTrackSinkVector::value_type& sinks : sinks_)` 相当) を、同じ `BaseRenderer::sinks_lock_` 保持中に実行する (キャンバス寸法と描画バッファのサイズ整合を確保する PR #357 の設計をそのまま維持)
5. `BaseRenderer::sinks_lock_` を解放してから `Render()` を呼ぶ (既存動作と同じ)

この方式なら既存のロック順序が維持され、デッドロックも自己ロックも発生しない。
`input_size_dirty_` のセット条件を「サイズが前回値から実際に変化したとき」に限定するため、`Sink::SetOutlineRect()` の呼び出しで `outline_changed_ = true` になっても、次の `Sink::OnFrame()` で入力サイズが不変なら再度 `input_size_dirty_` はセットされず、`outline_changed_` と `input_size_dirty_` の相互作用による無限ループは発生しない。

なお `Sink::SetOutlineRect()` は outline サイズ (width/height) の変更時に `outline_changed_ = true` をセットするため、方針 A/B による cols/rows 変更や共通縮小で対象 Sink の outline サイズが変わった場合には、対象 Sink は `Sink::OnFrame()` で再スケールされ、その 1 フレームは合成ループの `if (sink->GetOutlineChanged()) continue;` でスキップされる。これは既存の `SetSize()` / `AddTrack()` / `RemoveTrack()` 経路と同じ挙動であり、方針 C 由来で新たな長期スキップは生じない。outline サイズが変わらず outline_offset だけが変わったケースは early return により `outline_changed_` がセットされないため、対象 Sink の再スケールも走らない。

dirty 検出フェーズは毎フレーム全 Sink 分の `Sink::frame_params_lock_` 取得を追加するが、`BaseRenderer::RenderThread()` の合成ループも各 Sink の `Sink::frame_params_lock_` を取るため、**定常フレームでは 1 sink あたりの `frame_params_lock_` 取得回数が 1 → 2 に増える** (全体では sink 数分の追加取得)。合成ループはもともと `BaseRenderer::sinks_lock_` を保持したまま実行されるため、ロック保持区間の増加は最小限。dirty があるフレームでは、上記の 2 回に加えて `BaseRenderer::SetOutlines()` 内の代表 Sink 走査 (先頭から見つかるまで最大 sink 数分の `frame_params_lock_` 取得) と、outline サイズが変わった Sink 分の `Sink::SetOutlineRect()` のロック取得が発生する。これは cols/rows や枠サイズが実際に変化するタイミング (初回入力確定時と入力解像度変更時) に限られ、定常時の負荷ではない。

compose ループと dirty 検出を分離するのは、dirty があった場合に `BaseRenderer::SetOutlines()` を先に呼んで枠サイズを更新してから合成を行いたいためであり、統合すると `Sink::SetOutlineRect()` による無効化が同フレームの compose 結果に間に合わない。

## 完了条件

- `BaseRenderer::SetOutlines()` が方針 A + 方針 B を満たすこと
  1. `frame_aspect` を、`sinks_` の各 Sink の `Sink::input_width_` / `Sink::input_height_` から算出した実測アスペクトに基づいて決める。先頭から走査して有効な最初の Sink (代表 Sink) を採用し、代表 Sink を見つけた時点でループを抜けて残りの Sink はロックを取らない。走査中の各 Sink は `Sink::frame_params_lock_` を 1 件ずつ取得してローカルにコピーしてから解放する
  2. `sinks_` が空、またはどの Sink もまだ入力サイズを持たない場合は、既存の `STD_ASPECT` / `WIDE_ASPECT` 2 択ヒューリスティックにフォールバックする
  3. cols/rows を決定する `while` ループの条件式は既存と同じ
  4. cols/rows 決定後、方針 B の共通縮小と grid オフセット計算を float で行い、`grid_offset_x_f` / `grid_offset_y_f` は `std::max(0.0f, ...)` で必ず非負にクランプする。累積式の各境界も `std::min((float)width_, ...)` / `std::min((float)height_, ...)` で上側クランプしてから `std::floor` する。`Sink::SetOutlineRect()` に渡す `(x, y, width, height)` は隣接 cell 間の隙間が 0 になる累積方式で int に丸めた値を使う。フォールバック時 (方針 A の実測ができない状態) でも、実装単純化のため方針 B の共通縮小を同じロジックで適用する (視覚出力はまだ生じないため過渡的悪化は起きない)
- `Sink::SetOutlineRect()` は既存動作 (2 値 early return) をそのまま維持する (追加修正が不要な理由は「方針 B」節を参照)
- 方針 C の再計算経路が実装されていること
  1. `Sink` (`include/sora/renderer/base_renderer.h` の宣言と `src/renderer/base_renderer.cpp` のコンストラクタ初期化リスト) に `bool input_size_dirty_` フィールドを追加し、コンストラクタで false に初期化する
  2. `Sink::OnFrame()` の `webrtc::MutexLock lock(GetMutex());` 直後、既存の `if (outline_changed_ || frame.width() != input_width_ || frame.height() != input_height_)` 分岐の **外側 (前)** の位置に、`if (frame.width() != input_width_ || frame.height() != input_height_) input_size_dirty_ = true;` の 1 文を追加する (判定式に `outline_changed_` は含めない)。既存の外側 `if` 分岐と、その内部の `input_width_` / `input_height_` 書き換えには手を加えない
  3. `BaseRenderer::RenderThread()` が各イテレーションで `BaseRenderer::sinks_lock_` を取った直後に、各 Sink の `Sink::frame_params_lock_` を 1 件ずつ取得 → dirty 確認 → true の場合は false にリセット → 解放を繰り返して検出フェーズを完了し、1 件でも dirty があれば `BaseRenderer::sinks_lock_` 保持中に `BaseRenderer::SetOutlines()` を呼び直す
  4. `sinks_lock_ → Sink::frame_params_lock_` の既存ロック順序が維持され、デッドロックと自己ロックが発生しないこと
  5. dirty 検出 → SetOutlines → 描画準備 → 合成は同一の `BaseRenderer::sinks_lock_` 保持区間で行い、`Render()` は既存通り `BaseRenderer::sinks_lock_` 解放後に呼ぶ
- `Render()` のシグネチャは変更しない
- SDL / ANSI / Sixel 各レンダラーの実装は変更しない
- プロジェクトの E2E 実行規約 (CLAUDE.md) に従い、リポジトリルートで
  `uv run --directory=e2e-test pytest test_sumomo_basic.py::test_sumomo_sendonly_recvonly[VP8] -v -s --timeout=60`
  を実行し、既存の接続処理に回帰がないこと (SDL 経路を通らない一般回帰確認)
- 再現条件の 2 プロセス構成 (`--resolution FHD` 付き) で sumomo を起動し、以下をすべて確認する
  - **方針 C の dirty 経路の追認**: 方針 C の dirty 経路が動作していることを確認するため、`BaseRenderer::RenderThread()` の dirty 検出フェーズを完了した直後、方針 C 起因で `BaseRenderer::SetOutlines()` を呼び直す直前 (方針 C 手順 3 に相当する分岐の中) に一時的に `RTC_LOG(LS_INFO)` を仕込む (`SetOutlines()` は `AddTrack()` / `RemoveTrack()` / `SetSize()` / 方針 C の dirty 経路の 4 経路から呼ばれるため、`SetOutlines()` 冒頭に仕込むと起点を区別できない。上記の位置なら方針 C 起因の呼び出しに限定できる)。起動後、sink 0 と sink 1 の初回入力受信でそれぞれ dirty 経由の再計算が少なくとも 1 回発火することを確認する (`AddTrack(sink 0) → SetOutlines()` の時点は入力未受信でフォールバック、初回フレーム後に dirty 経由で実測に切り替わる。sink 1 追加後の初回フレームでも同様に dirty 経由の再計算が発火する)。ログ確認後、`RTC_LOG` は削除して commit しない
  - **方針 A の効果 (横並びケース)**: フルスクリーン 2560×1440 + sinks=2 + FHD (16:9) 映像で、修正前 (cols=1/rows=2、cell 2560×720、各 sink 左右 640 px 帯、cell 間ギャップ 0) から修正後にレイアウトが切り替わることを目視で確認する。修正後は方針 A で cols=2/rows=1、方針 B の共通縮小で `Sink::SetOutlineRect()` に渡る outline サイズは 1280×720 (`ideal_outline_width_f = 1280.0`、`ideal_outline_height_f = 1280.0 / (16/9) ≈ 720.0`)、`grid_offset_x_f = (2560.0 - 1280.0 * 2) / 2.0 = 0.0`、`grid_offset_y_f = (1440.0 - 720.0 * 1) / 2.0 = 360.0` 由来で上下に 360 px の外周帯となる。このケースの主効果は方針 A の cols/rows 切替である (cell の並び方向と映像アスペクトが整合するため方針 B の共通縮小は視覚差をほぼ生まない)。sumomo の自然な起動順では sink 0 が既にフレームを受信済みで sink 1 が追加されるため、sink 1 追加時の `AddTrack() → SetOutlines()` で直接この修正後レイアウトに落ちる (フォールバック期間 (cols=1/rows=2 相当) は原則観測されない)
  - **方針 B の効果 (縦積みケース)**: 再現条件の 2 プロセス構成のうちプロセス 1 の起動オプションを次のように変更する: `--fullscreen` を外し、`--window-width 720 --window-height 1440` を追加指定する。それ以外のオプション (`--use-sdl` / `--show-me` / `--fake-capture-device` / `--resolution FHD` / `--role sendrecv` / `--video-codec-type` / `--signaling-url` / `--channel-id`) はすべて維持する。この構成で縦長ウィンドウ + FHD (16:9) 映像 + sinks=2 の状態を作る。方針 A で cols=1, rows=2 になり、方針 A 単独 (方針 B の共通縮小を無効化した状態、`Sink::OnFrame()` の per-cell letterbox で処理) では、各 cell 720×720 に対し FHD 映像は cell 内で幅基準 downscale (`scaled_ = 720 < 1920 = true`) されて 720×405 になり、sink 0 が cell 0 内で上 157 px / 下 158 px (int 除算で非対称) letterbox され、cell 間の見かけギャップは合計約 315 px となる。方針 B の共通縮小を適用すると、`raw_outline_height_f = 720.0` を `content_aspect (≈1.7778)` で縮めた float 計算と累積式・`std::floor` により、`Sink::SetOutlineRect()` に渡る outline サイズは 720×405、`grid_offset_y_f` 由来の外周帯は 315 px にそれぞれ整数値で確定する。映像は (0, 315)〜(720, 720) と (0, 720)〜(720, 1125) に配置され、cell 間ギャップは 0 px、外周の上 315 px と下 315 px に余白が集約される。このケースの主効果は方針 B の cell 内 letterbox の外周集約である。実際の描画結果が上記の期待値と 1〜2 px の目視トレランス内で一致することを確認する
  - F キーによるフルスクリーン切替を 3 回以上繰り返してもクラッシュしないこと
  - 通常サイズ / フルスクリーンの往復で映像配置が破綻せず、`BaseRenderer::SetOutlines()` の呼び直しでレイアウトが正しく更新されること
- 変更履歴 (`CHANGES.md`) の `## develop` セクションの **コア SDK の** `[FIX]` 群 (`### misc` サブセクションではなく、`## develop` 直下の `[FIX]` 群) の **先頭** に、次を追記する (既存の `## develop` 直下の `[FIX]` 群は新しいものが上、古いものが下という順序で運用されている)

  ```text
  - [FIX] BaseRenderer の枠割りが映像アスペクトを無視して映像間に黒帯を広げる問題を修正する
    - @<担当者>
  ```

## 解決方法

(実装完了時に追記する)
