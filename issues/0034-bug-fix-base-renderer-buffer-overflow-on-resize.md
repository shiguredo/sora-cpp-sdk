# BaseRenderer の描画バッファがウィンドウリサイズ時に境界を超過する

- Priority: High
- Created: 2026-07-14
- Completed: {YYYY-MM-DD}
- Model: DeepSeek V4 Pro
- Branch: feature/fix-base-renderer-buffer-overflow-on-resize
- Polished: 2026-07-27

## 目的

`BaseRenderer::RenderThread()` の描画用バッファは、レンダラー起動時のキャンバス寸法で 1 回だけ確保される。
sumomo の `--use-sdl` モードでウィンドウを拡大すると、`width_` / `height_` だけが更新され、描画用バッファは起動時のサイズで残る。
次の描画フレームでは、更新後の寸法を使った `memset()` がバッファ境界を超えて write し、セグメンテーションフォールトまたはヒープ破壊を引き起こしうる。

## 優先度根拠

F キーによるフルスクリーン切替やウィンドウの手動拡大だけで境界外 write が発生する。
`memset()` は Sink の走査前に実行されるため、映像トラックの有無にかかわらず発生する。
ユーザー操作だけでプロセスの異常終了やヒープ破壊につながるメモリ安全性の問題であるため High。

## 現状

`BaseRenderer::RenderThread()` は、描画用バッファをループの前で 1 回だけ確保する。

```cpp
std::unique_ptr<uint8_t[]> image(new uint8_t[width_ * height_ * 4]);

while (running_) {
  memset(image.get(), 0, width_ * height_ * 4);
  // ...
  Render(image.get(), width_, height_, sink_infos);
}
```

`BaseRenderer::SetSize()` は `sinks_lock_` を取得し、`width_` / `height_` と各 Sink の outline を更新するが、描画用バッファを再確保しない。
sumomo の `SDLRenderer::PollEvent()` は `SDL_EVENT_WINDOW_RESIZED` を受け取ると `SetSize()` を呼ぶ。
このイベントは、F キーによるフルスクリーン切替、リサイズ可能なウィンドウの手動拡大、`--fullscreen` 起動時に発生しうる。

例として 640×480 のキャンバスで起動した場合、確保量は 1,228,800 bytes である。
その後 2560×1440 へ拡大すると、次の `memset()` は同じバッファへ 14,745,600 bytes を書くため、境界外 write が発生する。

`SetSize()` は sumomo のイベント処理スレッド、`RenderThread()` は専用の描画スレッドから実行されるため、両者は並行しうる。
現行コードでは、`memset()` と `Render()` の `width_` / `height_` 読み取りも `sinks_lock_` の外にあり、1 フレーム内で使用する寸法が一致する保証もない。

### 再現条件

sumomo をローカルの SDK 実装と紐付けてデスクトップ向けにビルドし、次のオプションを指定して起動する。
`--show-me --fake-capture-device` を使うことで、外部の配信者やカメラに依存せず Sink copy 経路まで実行する。

```text
--use-sdl
--show-me
--fake-capture-device
--role sendonly
--video-codec-type VP8
--signaling-url <URL>
--channel-id <ID>
```

起動後、次のいずれかを行う。

- F キーでフルスクリーンへ切り替える
- ウィンドウを起動時より大きく手動リサイズする
- `--fullscreen` を追加して起動し、初回のリサイズイベントが処理されるまで待つ

### 影響範囲

- **影響あり**: sumomo の `--use-sdl` モード
- **潜在リスクあり**: `BaseRenderer` を継承し、`SetSize()` を呼ぶ SDK 利用者の派生レンダラー
- **リポジトリ内で未顕在化**: Sixel / ANSI レンダラー。`BaseRenderer` を継承するが、リポジトリ内には `SetSize()` の呼び出し経路がない
- **影響なし**: sdl_sample。`BaseRenderer` を継承せず、各 Sink の画像を独自に描画する

### 本 issue のスコープ外

- `width_ * height_ * 4` が `size_t` の範囲を超える場合の包括的な整数オーバーフロー対策
- `SetSize()` へ 0 以下の寸法が渡された場合の入力検証と挙動定義
- `SetSize()` の呼び出し規約の API 文書化
- 描画用バッファと無関係な既存の並行処理設計

本修正の再現条件と完了条件は、SDL から通知される正の寸法を対象とする。
`resize()` の引数型に合わせて `static_cast<size_t>` してから乗算することは、包括的なオーバーフロー対策とは別に本修正へ含める。

## 設計方針

描画用バッファを `std::vector<uint8_t>` に変更し、ループの外で宣言して capacity をイテレーション間で再利用する。

各イテレーションでは、`sinks_lock_` を保持した単一のクリティカルセクション内で次を行う。

1. `width_` / `height_` を `canvas_width` / `canvas_height` へスナップショットする
2. 同じ寸法から算出したサイズへ描画用バッファを `resize()` する
3. `image.size()` を使って描画用バッファ全体を `memset()` でクリアする
4. 同じ `canvas_width` を宛先 stride と Y offset の計算に使って各 Sink を合成する

寸法のスナップショットから Sink の合成まで同じロックを保持することで、その途中に `SetSize()` が入り、キャンバス寸法と outline が食い違うことを防ぐ。
`resize()` と全バッファのクリアもロック内へ移るため、毎フレームのロック保持時間は現行より増える。
ただし、寸法と書き込み範囲の一貫性を優先し、この影響を許容する。

`Render()` は同じ寸法スナップショットと `image.data()` を受け取るが、現行どおり `sinks_lock_` の外で呼ぶ。
`Render()` をロック内で呼ぶと、同期的な dispatch 実装から `SetSize()` が呼ばれた場合に自己デッドロックするためである。

`Render()` のシグネチャは変更しないため、公開 ABI への影響はない。
`vector::resize()` により内部ポインタはイテレーション間で変化しうるが、リポジトリ内の SDL / ANSI / Sixel レンダラーは、`Render()` から戻るまでに同期的に画像を消費し、ポインタを保持しない。

## 完了条件

- `BaseRenderer::RenderThread()` が次をすべて満たすこと
  1. 描画用バッファが `std::vector<uint8_t>` であり、ループの外で宣言されている
  2. 毎イテレーション、`sinks_lock_` 保持中にキャンバス寸法を `canvas_width` / `canvas_height` へスナップショットしている
  3. `sinks_lock_` 保持中に `image.resize(static_cast<size_t>(canvas_width) * static_cast<size_t>(canvas_height) * 4)` と `memset(image.data(), 0, image.size())` を実行している
  4. `ARGBCopy()` の宛先 stride と Y offset の計算、および `Render()` の引数が同じ `canvas_width` / `canvas_height` だけを使い、`resize()` 後に `width_` / `height_` を再読していない
  5. Sink のフレーム寸法には、`GetFrameWidth()` / `GetFrameHeight()` から得た別のローカル変数を使っている
  6. `GetOutlineChanged()` が true の Sink を処理しない現行分岐を維持している
  7. `Render()` を `sinks_lock_` の外で呼んでいる
- SDL / ANSI / Sixel レンダラーが `Render()` から戻った後に `image` を保持していないことをコードレビューで確認する
- sumomo をローカルの SDK 実装と紐付けてビルドできること
- プロジェクトの E2E 実行規約に従って `test_sumomo_sendonly_recvonly[VP8]` を実行し、既存の接続処理に回帰がないこと
  - この E2E は SDL レンダラー経路を通らない一般回帰確認であり、本バグ自体は次の手動確認で検証する
- 再現条件に記載したオプションで sumomo を起動し、次をすべて確認する
  - F キーによるフルスクリーン切替を、入る・戻るの 1 往復として 3 回以上繰り返してもクラッシュしない
  - ウィンドウを起動時より大きく手動リサイズしてもクラッシュしない
  - `--fullscreen` 起動後、初回のリサイズイベントを処理してもクラッシュしない
  - フルスクリーン解除後も fake 映像の表示が継続する
- AddressSanitizer を有効にして SDK と sumomo をビルドし、同じ手動確認で `heap-buffer-overflow` が検出されないこと
  - macOS または Ubuntu のクリーンなビルド構成を使う
  - SDK の既存 `CMAKE_CXX_FLAGS` を維持したまま `-fsanitize=address` を追加し、sumomo のコンパイルとリンクにも `-fsanitize=address` を指定する
  - 再現条件に記載した 3 種類のリサイズ操作をすべて実行する
  - libwebrtc 内部の既知の擬陽性を除外するため、実行時は `ASAN_OPTIONS=detect_container_overflow=0` を指定する
  - この検証は CMake の設定を使って手動で行い、ビルドスクリプトへの ASan オプション追加は本 issue に含めない
- 変更履歴の develop にあるコア SDK の `[FIX]` 群へ、次を追記する

  ```text
  - [FIX] BaseRenderer の描画バッファがウィンドウリサイズ時に境界を超過する問題を修正する
    - @<担当者>
  ```

## 解決方法

`BaseRenderer::RenderThread()` を次の順序へ変更する。
`<vector>` と `<cstring>` は既に利用可能なため、追加の依存は不要である。
以下は描画用バッファに関係する変更箇所だけを示し、フレームレート制御は省略する。

```cpp
std::vector<uint8_t> image;

while (running_) {
  std::vector<SinkInfo> sink_infos;
  int canvas_width = 0;
  int canvas_height = 0;

  {
    webrtc::MutexLock lock(&sinks_lock_);
    canvas_width = width_;
    canvas_height = height_;
    image.resize(static_cast<size_t>(canvas_width) *
                 static_cast<size_t>(canvas_height) * 4);
    memset(image.data(), 0, image.size());

    for (const VideoTrackSinkVector::value_type& sinks : sinks_) {
      Sink* sink = sinks.second.get();
      webrtc::MutexLock frame_lock(sink->GetMutex());
      if (sink->GetOutlineChanged()) {
        continue;
      }

      int width = sink->GetFrameWidth();
      int height = sink->GetFrameHeight();
      if (width == 0 || height == 0) {
        continue;
      }

      libyuv::ARGBCopy(sink->GetImage(), width * 4,
                       image.data() + sink->GetOffsetX() * 4 +
                           sink->GetOffsetY() * canvas_width * 4,
                       canvas_width * 4, width, height);

      // SinkInfo の構築は現行どおり
    }
  }

  Render(image.data(), canvas_width, canvas_height, sink_infos);
}
```

### 注意点

- `resize()` より前に取得した `data()` は再確保によって無効になりうるため、必ず `resize()` の後で `image.data()` を使う
- Sink ごとの `image_` は `Sink::OnFrame()` 内で outline または入力解像度が変わったときに再確保されるため、本修正の対象外とする
- ロック順序は現行どおり `sinks_lock_` から Sink の mutex の順とし、逆順の取得を追加しない
