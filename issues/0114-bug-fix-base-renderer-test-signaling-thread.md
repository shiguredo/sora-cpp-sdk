# BaseRenderer テストが VideoTrack を signaling スレッド以外で作るため Debug ビルドで abort する

- Created: 2026-10-16
- Completed: 2026-10-17
- Branch: feature/fix-base-renderer-test-signaling-thread
- Polished: {YYYY-MM-DD}

## 目的

`test/base_renderer.cpp` が `webrtc::VideoTrack` を signaling スレッド以外で作っているため、libwebrtc の DCHECK が有効なビルド (Debug) で `base_renderer.exe` が abort するのを防ぐ。Release では DCHECK が無効なため素通りしており、Debug ビルドのテストが実行できない状態になっている。

## 現状

### 発生事象

libwebrtc をソースビルドした Debug (`is_debug = true`) を利用して `python run.py build windows_x86_64 --debug --test` を実行すると、ビルドは通るが `base_renderer.exe` が abort する。3 回実行して 3 回とも再現する。

```text
Randomness seeded to: 475817749

#
# Fatal error in: ..\..\..\..\_source\windows_x86_64\webrtc\src\pc\video_track.cc, line 47
# last system error: 0
# Check failed: (&signaling_thread_)->IsCurrent()
# # Expected: TQ: 000002188E209090 Thread: 0000000000009AF8
# Actual:   TQ: 0000000000000000 Thread: 0000000000008164
TaskQueue doesn't match
```

終了コードは `-2147483645` (`0x80000003`、ブレークポイント) で、`TestRenderer::WaitUntil()` を待たずにテストプロセスが終了する。Release では同じテストが 10 ケース全て成功する。

### 原因

libwebrtc の `webrtc::VideoTrack` は、コンストラクタで `RTC_DCHECK_RUN_ON(&signaling_thread_)` を呼ぶ。`signaling_thread_` は `RTC_NO_UNIQUE_ADDRESS SequenceChecker` のメンバで、`VideoTrack::Create` を signaling スレッド上で呼ぶことで初めてそのスレッドに束縛される。束縛されていない状態で別のスレッドから呼ばれると `IsCurrent()` が偽になり、DCHECK が失敗する。

`test/base_renderer.cpp` の `TrackFixture` は `webrtc::Thread::Create()` で作った `worker` スレッドしか用意しておらず、signaling スレッドを作っていない。`CreateTrackAndAddSink()` は `worker->BlockingCall()` の中で `webrtc::VideoTrack::Create(...)` を呼ぶため、signaling スレッドに束縛されないまま `VideoTrack` が作られる。

DCHECK は `rtc_base/checks.h` の `RTC_DCHECK_IS_ON` で制御され、この値は `NDEBUG` の有無で決まる。Release のコンパイルフラグには `/DNDEBUG` が含まれるため `RTC_DCHECK_IS_ON` は 0 になり、この検査は実行されない。Debug のコンパイルフラグには `NDEBUG` が含まれないため `RTC_DCHECK_IS_ON` は 1 になり、検査が実行されて abort する。

そのため、この問題は Release では検出できず、Debug ビルドを実行して初めて現れる。

### 再現手順

libwebrtc をソースビルドした Debug を用意し、`--local-webrtc-build-dir` を指定して Debug ビルドを作る。ダウンロードして利用する prebuilt の libwebrtc はリリースビルドの `webrtc.lib` しか提供しないため、この手順にはソースビルドの libwebrtc が必要になる。

```text
python run.py build windows_x86_64 --debug --test --local-webrtc-build-dir <webrtc-build のパス>
_build\windows_x86_64\debug\test\base_renderer.exe
```

`base_renderer.exe` を引数なしで実行すると、最初の `TEST_CASE` で上記の `Check failed` が出る。他のテスト (`video_factory_env` / `video_factory_data_race` / `sora_client_context` / `device_list`) は Debug でも成功する。

## 設計方針

- `test/base_renderer.cpp` の `TrackFixture` に signaling スレッドを用意し、`webrtc::VideoTrack::Create` を signaling スレッド上で呼ぶ。`AddOrUpdateSink` と `RemoveSink` は worker スレッド上で呼ぶ必要があるため、`TrackFixture` は worker と signaling の両方のスレッドを持つ
- `TrackFixture` のデストラクタで signaling スレッドの後片付けも行い、`VideoTrack` の破棄も signaling スレッド上で行う
- libwebrtc の `VideoTrack` は worker と signaling の 2 つのスレッドを前提とするため、テスト側もその前提に合わせる。モックやスタブは利用しない
- 変更対象は `test/base_renderer.cpp` のみとする。`src/renderer/base_renderer.cpp` の `BaseRenderer` は変更しない

## 完了条件

- ソースビルドした libwebrtc の Debug で `base_renderer.exe` の全 10 ケースが成功し、`Check failed` が出ない
- Release でも `base_renderer.exe` の全 10 ケースが成功し、既存の枠割り・拡大縮小・回転の検証に回帰がない
- モックやスタブを利用しない
- `python run.py build windows_x86_64 --debug --test` が実 Sora を必要としないテストまで通る

## 関連 issue

- 「Windows のビルドを MSVC から libwebrtc と同じ clang-cl + libc++ に変更する」: このテストが Debug で動かせるようになったことで判明した。prebuilt の libwebrtc では Debug ビルドができないため、この問題はその変更が入るまで表面化しなかった
- 「BaseRenderer テストが描画前の初期状態を成功と判定する問題を修正する」: 同じ `test/base_renderer.cpp` の `TrackFixture` を扱うが、対象は待機条件でありこの issue とは別の問題

## 解決方法

`test/base_renderer.cpp` の `TrackFixture` が生の `webrtc::VideoTrack` をそのまま `BaseRenderer` に渡していたのをやめ、`VideoTrackProxy` で包んだトラックを渡すようにした。生の `VideoTrack` は Sink の追加と削除を worker スレッドで、破棄を signaling スレッドで行う必要があるが、`VideoTrackProxy` はこれらの呼び出しをそれぞれのスレッドへ振り分けるため、テスト側でスレッドを選ぶ必要がなくなる。

- `TrackFixture` に `webrtc::Thread::Create()` で作る `signaling` を追加し、`worker` とともに `Start()` する
- `CreateTrackAndAddSink()` は `signaling->BlockingCall()` の中で `webrtc::VideoTrack::Create()` を呼ぶ。生の `VideoTrack` の生成は signaling スレッド上で行う必要があるため
- 生成した生の `VideoTrack` を `webrtc::VideoTrackProxy::Create(signaling, worker, raw)` で包み、これを `BaseRenderer` に追加する。`AddOrUpdateSink` / `RemoveSink` は proxy が worker スレッドへ、proxy 自身の破棄は signaling スレッドへ振り分ける
- トラックを保持する型を `webrtc::scoped_refptr<webrtc::VideoTrack>` から `webrtc::scoped_refptr<webrtc::VideoTrackInterface>` に変更した。`VideoTrackProxy::Create()` が返すのは `VideoTrackProxyWithInternal<VideoTrackInterface>` のため
- デストラクタでは `renderer.RemoveTrack()` で Sink を破棄してから `tracks` を `clear()` する。`Sink` が行う `RemoveSink` も proxy が worker スレッドへ寄せるため、`worker->BlockingCall()` で囲む必要がなくなった

モックやスタブは利用していない。変更対象は `test/base_renderer.cpp` のみで、`src/renderer/base_renderer.cpp` の `BaseRenderer` は変更していない。

### 検証結果

libwebrtc をソースビルドした Debug (`is_debug = true`) と Release を `--local-webrtc-build-dir` で指定して `python3 run.py build ubuntu-24.04_x86_64 --debug --test` および `python3 run.py build ubuntu-24.04_x86_64 --test` を実行し、`base_renderer` の挙動を確認した。

- 修正前: Debug で最初の `TEST_CASE` が `pc/video_track.cc` の `Check failed: (&signaling_thread_)->IsCurrent()` を出して SIGABRT で終了する (issue の再現手順どおり)
- 修正後: Debug / Release ともに `All tests passed (29 assertions in 10 test cases)` で 10 ケース全て成功する。それぞれ 3 回連続で実行しても同じ結果になる

同じ Debug ビルドで `video_factory_env` / `video_factory_data_race` / `sora_client_context` も成功する。`device_list` はこの環境に V4L2 のキャプチャデバイスが無いため失敗するが、修正とは無関係である。

なお `--local-webrtc-build-dir` を指定して libwebrtc をソースビルドした場合、NVCODEC の CUDA ソースがコンパイルエラーになる既存の問題があるため、検証では `--disable-cuda` を併用した。
