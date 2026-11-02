# BaseRenderer テストの描画前待機が描画スレッドの起動タイミングで失敗する

- Created: 2026-11-02
- Completed: {YYYY-MM-DD}
- Branch: feature/fix-base-renderer-test-start-race
- Polished: {YYYY-MM-DD}

## 目的

CI で `base_renderer` テストが間欠的に失敗するのを防ぐ。

`test/base_renderer.cpp` の「BaseRenderer のテスト用レンダラーが描画前に空の矩形の待機を成功させない」ケースは、描画スレッドを起動していない状態を作るために、`TestRenderer` のコンストラクタが呼ぶ `Start()` の直後に `Stop()` を呼んでいる。
この方法は、`Start()` から `Stop()` までの間に描画スレッドが 1 回目の `Render()` を完了するかどうかというスケジューリング競合に依存しており、間欠的に失敗する。

テストの前提を競合に依存しない形にして、CI の偽の失敗で他のテストが実行されず、リリース判定が妨げられる状態を解消する。

## 現状

### 発生事象

2026-10-30 の scheduled run の [ci run 41013179177 の macos_arm64 ジョブ](https://github.com/shiguredo/sora-cpp-sdk/actions/runs/41013179177/job/123923436412) で、`python3 run.py build --test --run-e2e-test --package macos_arm64` が失敗した。
コミットは `2ee59d9f` で、失敗したテストケースは「BaseRenderer のテスト用レンダラーが描画前に空の矩形の待機を成功させない」である。

```text
/Users/runner/work/sora-cpp-sdk/sora-cpp-sdk/test/base_renderer.cpp:552: FAILED:
  REQUIRE( !observed )
with expansion:
  false

test cases: 11 | 10 passed | 1 failed
assertions: 30 | 29 passed | 1 failed
subprocess.CalledProcessError: Command '[.../_build/macos_arm64/release/test/base_renderer']' returned non-zero exit status 42.
```

終了コード 42 は Catch2 がテスト失敗時に返す値である。ビルド自体と、同じジョブ内の `e2e` は成功しており、製品コードのビルドは壊れていない。
同一 run の他の 10 ジョブ (windows / ubuntu 6 種 / android / ios / raspberry-pi-os) はすべて成功している。

`run.py` は `base_renderer` で停止したため、後続の `sora_client_context` / `video_factory_env` / `audio_adaptive_ptime` は実行されていない。
また `e2e-test` ジョブは `build-macos` の失敗により skipped になっている。

### 原因

`test/base_renderer.cpp` の `TestRenderer` のコンストラクタは `Start()` を呼んで描画スレッドを起動する。
失敗したテストケースは、その直後に `Stop()` を呼ぶことで「描画スレッドを起動していない状態」を作ろうとしている。

しかし `BaseRenderer::Start()` が描画スレッドを生成してから `BaseRenderer::Stop()` が `running_` を偽にするまでの間に、描画スレッドが `BaseRenderer::RenderThread()` の 1 周目を実行して `TestRenderer::Render()` を呼ぶことがある。
`TestRenderer::Render()` は観測状態を更新して `rendered_` を真にするため、`Stop()` の後で呼ばれる `TestRenderer::WaitUntil()` の述語 `rendered_ && predicate(...)` が、`sink_infos_` が空の時点で即座に成立する。
その結果 `REQUIRE(!observed)` が失敗する。

`BaseRenderer::Stop()` は `thread_->join()` するだけで、`TestRenderer` が持つ観測状態 (`rendered_` / `canvas_width_` / `canvas_height_` / `canvas_` / `sink_infos_`) を初期状態へ戻さない。
そのため、コンストラクタ内の `Start()` から `Stop()` の間に 1 回でも `Render()` が走るとテストの前提が崩れる。

`BaseRenderer::RenderThread()` は `running_` を確認してから 1 周分の処理を行うため、`Stop()` が `running_` を偽にする前に描画スレッドが 1 周目へ入れば `Render()` は呼ばれる。
どちらが先になるかはスケジューリング次第で決まり、テスト側に同期はない。

### 発生が macOS ランナーに限られる理由

同じコミット `2ee59d9f` でも、windows / ubuntu / android / ios / raspberry-pi-os のジョブは成功している。
スレッド生成直後に描画スレッドがメインスレッドより先に 1 周目を走るかどうかに依存する競合のため、runner のスケジューリングによって再現性が変わる。

### 再現手順

`test/base_renderer.cpp` の該当テストケースで、`TestRenderer` を構築した直後、`Stop()` を呼ぶ前に 100 ms の待機を入れると、描画スレッドが確実に 1 回目の `Render()` を完了するため 100% 失敗する。

```cpp
TestRenderer renderer(1, 1);
// 競合を再現するための待機 (この間に描画スレッドが Render() を呼ぶ)
std::this_thread::sleep_for(std::chrono::milliseconds(100));
renderer.Stop();
bool observed = renderer.WaitUntil(
    [](const std::vector<ActualRect>& actual) { return actual.empty(); });
REQUIRE(!observed);
```

失敗する内容は、上記の CI のログと一致する。

### 再現性

- 2026-10-30 の scheduled run で macos_arm64 が失敗
- 2026-10-26 / 10-27 / 10-28 / 10-29 の scheduled run はすべて成功
- 2026-10-23 の [scheduled run 39911130834](https://github.com/shiguredo/sora-cpp-sdk/actions/runs/39911130834) でも同じ macos_arm64 ジョブが失敗している (ログは保持期限切れで、同一テストケースかは未確認)

### 既存 issue との関係

- 0111 は「描画前に空の矩形の待機が成功しない」検証を追加した issue である。0111 のテスト戦略には「テスト用レンダラーの自動起動を抑制できるようにし」とあるが、実装では自動起動を抑制する仕組みを作らず、コンストラクタ内の `Start()` の直後に `Stop()` する方法を採っている。本 issue はこの実装の競合を対象とする
- 0114 は同じ `test/base_renderer.cpp` の `TrackFixture` を扱うが、対象は signaling スレッド制約であり、本 issue とは別である
- 0005 は E2E テスト (pytest) の間欠失敗に対する retry の話であり、C++ のユニットテストの競合を対象とする本 issue とは対象が異なる

open / closed / pending の issue、CHANGES.md、直近のコミット履歴に、同じ競合を扱った記録は見つからなかった。

## 設計方針

- `TestRenderer` のコンストラクタに描画スレッドを起動するかどうかの引数を追加し、起動を抑止できるようにする。0111 のテスト戦略で想定していた構成に戻す
- 上記の引数で描画スレッドを起動しなかった場合、テストケースは `Stop()` を呼ばない。これにより `Start()` と `Stop()` の競合自体が無くなる
- 描画スレッドを起動しない状態で `WaitUntil()` が偽を返すことの確認と、その後の `Start()` で 1x1 のキャンバスと空の矩形が観測できることの確認は現状を維持する
- 変更対象は `test/base_renderer.cpp` のみとする。`src/renderer/base_renderer.cpp` の `BaseRenderer` は変更しない
- モックやスタブは利用しない

## テスト戦略

実際の `BaseRenderer`、WebRTC トラック、映像フレーム、描画スレッドを使う。モックやスタブは利用しない。

- 修正前のコードに上記の再現手順の待機を入れると、該当ケースが CI と同じ内容で失敗することを確認する
- 修正後のコードで `base_renderer` の全 11 ケースが成功することを確認する
- 描画スレッドを起動しない状態で `WaitUntil()` がタイムアウトにより偽を返すことを確認する
- 描画スレッドを起動しない状態から `Start()` した後、1x1 のキャンバスと空の矩形が観測できることを確認する
- `WaitUntil()` から `rendered_` の判定を外すと、このケースが失敗することを確認する。0111 で追加した待機条件の検証が引き続き機能していることを確かめる
- 該当ケースを単独で反復実行し、失敗しないことを確認する
- macos_arm64 をはじめとする各ターゲットの CI で成功することを確認する

## 完了条件

- 描画前の待機を検証するケースが、描画スレッドの起動タイミングに依存せず決定的に偽を返す
- 描画スレッドを起動しない `TestRenderer` から `Start()` した後、1x1 のキャンバスと空の矩形が観測できる
- `base_renderer` の 11 ケースがすべて成功し、枠割り・拡大縮小・回転・極小状態の検証に回帰がない
- モックやスタブを利用していない
- macos_arm64 を含む CI の全ジョブが成功する

## 解決方法

{どのように対応するのかを明確にすること}
