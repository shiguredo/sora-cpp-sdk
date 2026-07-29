# BaseRenderer の SetOutlines がフルスクリーン時に映像間に黒帯を広げる

- Priority: Medium
- Created: 2026-07-29
- Completed: {YYYY-MM-DD}
- Model: Opus 4.7
- Branch: feature/fix-set-outlines-use-frame-aspect
- Polished: {YYYY-MM-DD}

## 目的

`--fullscreen` + 複数 sinks 構成で sumomo を起動したとき、映像と映像の間に横方向・縦方向とも大きな黒帯が広がる。
`BaseRenderer::SetOutlines()` の枠割りロジックが、実際の入力映像アスペクトを無視して 2 択ヒューリスティックだけで cols/rows を決めているため、フルスクリーンの横長ウィンドウに対して**縦長の枠**が選ばれ、そこに横長の映像を入れた結果、各枠内で上下に大きな余白が生じる。

これを修正し、Sink 群の実際の映像アスペクト実測値に基づいて cols/rows を決めることで、フルスクリーン時の映像配置の余白を最小化する。

## 優先度根拠

- クラッシュや異常終了は発生せず、機能上の致命的な影響はない
- ただし `--fullscreen` 起動時にユーザーの体感上、映像の位置が「離れて見える」ため視認性が明確に損なわれる
- 直近 #357 (`70df13e3`) で描画バッファ境界超過を修正した結果、フルスクリーン描画が回るようになって顕在化した現象であり、修正の妥当性を裏付ける後続対応として重要
- クラッシュではないため Medium とする

## 現状

`src/renderer/base_renderer.cpp:278-323` の `SetOutlines()` は、ウィンドウのアスペクト比のみを基準に cols/rows を決めている。

```cpp
static constexpr float STD_ASPECT = 1.33f;   // 4:3
static constexpr float WIDE_ASPECT = 1.78f;  // 16:9

void BaseRenderer::SetOutlines() {
  float window_aspect = (float)width_ / (float)height_;
  bool window_is_wide = window_aspect > ((STD_ASPECT + WIDE_ASPECT) / 2.0);
  float frame_aspect = window_is_wide ? WIDE_ASPECT : STD_ASPECT;
  ...
}
```

`frame_aspect` は `STD_ASPECT (1.33)` と `WIDE_ASPECT (1.78)` の 2 値からしか選ばれず、実際の入力映像のアスペクトとの整合はまったく取られていない。
各 Sink は割り当てられた枠 (outline) の中で映像アスペクトを維持してセンタリングされる (`Sink::OnFrame()` の分岐) ため、`frame_aspect` と実映像アスペクトが乖離すると、そのぶん枠内で黒帯が広がる。

### 再現条件

sumomo をローカルの SDK 実装と紐付けてビルドし、以下のオプションで起動する。

```text
--use-sdl
--show-me
--fake-capture-device
--fullscreen
--role sendonly
--video-codec-type VP8
--signaling-url <URL>
--channel-id <ID>
```

もしくは通常サイズで起動後、F キーでフルスクリーンに切り替える。
`--show-me` によりローカル映像と自映像で sinks 数が 2 以上となる構成を作る。

### 具体例

フルスクリーン 2560×1440 (アスペクト 1.78) で sinks=2 の場合:

- `window_aspect = 1.78`、`window_is_wide = true`、`frame_aspect = WIDE_ASPECT = 1.78`
- `times = floor(1.78 / 1.78) = 1`
- `rows*cols < 2` かつ `times < cols/rows` は `1 < 1 = false` → `cols++`
- 結果: **cols=2, rows=1** → 各枠は **1280×1440 (アスペクト 0.89 の縦長)**

そこに 16:9 (アスペクト 1.78) の映像を入れると:

- `frame_aspect (1.78) > outline_aspect (0.89)` → `width = 1280`、`height = 1280 / 1.78 ≈ 719`
- 枠内で上下に `(1440 - 719) / 2 = 360` px ずつ黒帯

結果、2 つの映像それぞれの上下に 360px の黒帯ができ、画面全体で見ると映像の間に横方向にも縦方向にも大きな黒領域が広がる。

### 本 issue のスコープ外

- 映像アスペクトが sink ごとに大きく異なる場合の「どの sink を代表とするか」の詳細な UX 検討 (最小限のルールを設計方針で示す)
- 枠アスペクトを補正して枠内 letterbox を減らす経路 (対案 2 として検討したが、根本原因の cols/rows 選択には効かないため本 issue では採用しない)
- `Render()` シグネチャや SDL/ANSI/Sixel 各レンダラー側の変更
- `SetOutlines()` 以外の描画・スケール処理

## 設計方針

`SetOutlines()` の `frame_aspect` を、`WIDE_ASPECT` / `STD_ASPECT` の 2 値ヒューリスティックから、Sink 群の実測アスペクトに置き換える。

1. `SetOutlines()` で `frame_aspect` を求める際、現在の `sinks_` を走査し、各 Sink の `input_width_` / `input_height_` からアスペクトを計算する。有効な (0 でない) アスペクトを持つ Sink が 1 つでもあれば、その平均か最初の 1 つを `frame_aspect` に採用する。
2. どの Sink も `input_width_` / `input_height_` を持たない初期状態 (最初の `OnFrame()` が来る前) では、既存の `STD_ASPECT` / `WIDE_ASPECT` ヒューリスティックにフォールバックする。
3. `Sink::OnFrame()` が呼ばれて `input_width_` / `input_height_` が初めて確定 (または変化) したタイミングで、`SetOutlines()` を再計算する経路を追加する。`OnFrame()` は Sink 固有のロックのみを保持しているため、`sinks_lock_` を取り直したうえで `SetOutlines()` を呼ぶ必要がある (デッドロック回避を検証する)。
4. cols/rows 決定ロジック自体 (`while` ループの条件) はそのまま維持し、`frame_aspect` の入力ソースだけを差し替える。

これにより、フルスクリーン 16:9 (1.78) + 16:9 映像 + sinks=2 の場合:

- `frame_aspect = 1.78`、`window_aspect = 1.78`
- `times = 1`、`cols/rows = 1`
- `times < cols/rows` は `false` → `cols++` (現状と同じ挙動)

...のように**同じ結果**になるケースもあるが、映像が例えば 4:3 (1.33) の場合:

- `frame_aspect = 1.33`、`window_aspect = 1.78`
- `times = floor(1.78 / 1.33) = 1`
- 現状より映像アスペクトに近い基準で cols/rows を判定するため、枠アスペクトと映像アスペクトの乖離が小さくなる

映像が縦長 (アスペクト < 1.0) の場合も、映像アスペクトを直接使えば `window_aspect / frame_aspect` の判定が実映像の形に沿う。

## 完了条件

- `BaseRenderer::SetOutlines()` が次を満たすこと
  1. `frame_aspect` を、`sinks_` の各 Sink の `input_width_` / `input_height_` から算出した実測アスペクトに基づいて決める
  2. どの Sink もまだ入力サイズを持たない場合は、既存の `STD_ASPECT` / `WIDE_ASPECT` ヒューリスティックにフォールバックする
  3. cols/rows を決定する `while` ループの条件式は既存と同じ
- `Sink::OnFrame()` で `input_width_` / `input_height_` が初回確定または変化したときに、`SetOutlines()` を再計算する経路が存在すること
  - `sinks_lock_` と Sink 固有ロックの取得順序が既存と一致し、デッドロックが発生しないこと (`OnFrame()` は Sink ロック中のため、`sinks_lock_` の取得はロック解放後か、順序整合を検証してから行う)
- `Render()` のシグネチャは変更しない
- SDL / ANSI / Sixel 各レンダラーの実装は変更しない
- プロジェクトの E2E 実行規約に従って `test_sumomo_sendonly_recvonly[VP8]` を実行し、既存の接続処理に回帰がないこと
- 再現条件のオプションで sumomo を起動し、以下をすべて確認する
  - フルスクリーンで sinks=2 の場合、映像が横または縦に並び、各映像の枠内での黒帯が現状より明確に小さくなること
  - フルスクリーンで sinks=1 の場合、映像が中央に配置され、黒帯が対称であること
  - F キーによるフルスクリーン切替を 3 回以上繰り返してもクラッシュしないこと
  - 通常サイズ / フルスクリーンの往復で映像配置が破綻しないこと
- 変更履歴 (`CHANGES.md`) の develop にあるコア SDK の `[FIX]` 群へ、次を追記する

  ```text
  - [FIX] BaseRenderer の SetOutlines がフルスクリーン時に映像間に黒帯を広げる問題を修正する
    - @<担当者>
  ```

## 解決方法

(実装完了時に追記する)
