# 全サンプルの OnSetOffer 内 AddTrack の RTCErrorOr 戻り値が無視されている

- Priority: High
- Created: 2026-07-10
- Completed: {YYYY-MM-DD}
- Model: DeepSeek V4 Pro
- Branch: feature/fix-examples-addtrack-return-value
- Polished: {YYYY-MM-DD}

## 目的

`examples/sumomo/src/sumomo.cpp`, `examples/sdl_sample/src/sdl_sample.cpp`, `examples/messaging_recvonly_sample/src/messaging_recvonly_sample.cpp` の全サンプルで `OnSetOffer` 内の `AddTrack` 呼び出しの戻り値 (`RTCErrorOr`) が変数に代入されているが一度も参照されていない。`AddTrack` が失敗してもサイレントに継続され、コンパイラ警告の対象にもなる。

## 優先度根拠

SDP 不一致等で `AddTrack` が失敗した場合、メディアトラックが追加されず音声/映像が流れないが、エラーは一切出力されない。サンプルコードとして誤った実装パターンを示しており High。

## 現状

`examples/sdl_sample/src/sdl_sample.cpp:129-135`:

```cpp
auto audio_result = pc->AddTrack(audio_source, {stream_id});
auto video_result = pc->AddTrack(video_source, {stream_id});
// audio_result, video_result が参照されない
```

## 設計方針

戻り値をチェックし、失敗時は `RTC_LOG(LS_ERROR)` を出力する。もしくは `[[maybe_unused]]` を付与して意図的に無視していることを明示する。

## 完了条件

- 全サンプルで `AddTrack` の戻り値がチェックされるか、意図的な無視が明示されること
