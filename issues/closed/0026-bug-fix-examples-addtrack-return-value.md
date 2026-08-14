# sumomo と sdl_sample の OnSetOffer 内 AddTrack の RTCErrorOr 戻り値が無視されている

- Priority: Medium
- Created: 2026-07-10
- Completed: 2026-07-14
- Model: DeepSeek V4 Pro
- Branch: feature/fix-examples-addtrack-return-value
- Polished: 2026-07-10
## 目的

`examples/sumomo/src/sumomo.cpp` と `examples/sdl_sample/src/sdl_sample.cpp` の `OnSetOffer` 内で、`AddTrack` の戻り値 (`webrtc::RTCErrorOr`) が変数 (`audio_result` / `video_result`) に代入されているが一度も参照されていない。`AddTrack` が失敗してもサイレントに継続され、エラーが一切出力されない。サンプルコードとして、戻り値のエラーを握りつぶす誤った実装パターンを示している。

なお `examples/messaging_recvonly_sample/src/messaging_recvonly_sample.cpp` は受信専用サンプルであり、`OnSetOffer` (`messaging_recvonly_sample.cpp:105`) は空実装で `AddTrack` を呼ばないため対象外とする。

## 優先度根拠

`AddTrack` は PeerConnection がすでに閉じられている、同一トラックが二重に追加されるなどの場合に失敗し得るが、通常の送信系サンプル (sendonly / sendrecv) の接続シーケンスでは成功する。実運用でのクラッシュや状態破綻には直結しないため実害は限定的だが、サンプルコードがエラーを握りつぶすパターンを示している点、および失敗時にトラックが追加されず音声/映像が流れないのにエラーが出ない点を是正する。Medium。

## 現状

`examples/sdl_sample/src/sdl_sample.cpp:127-136`:

```cpp
if (audio_track_ != nullptr) {
  webrtc::RTCErrorOr<webrtc::scoped_refptr<webrtc::RtpSenderInterface>>
      audio_result =
          conn_->GetPeerConnection()->AddTrack(audio_track_, {stream_id});
}
if (video_track_ != nullptr) {
  webrtc::RTCErrorOr<webrtc::scoped_refptr<webrtc::RtpSenderInterface>>
      video_result =
          conn_->GetPeerConnection()->AddTrack(video_track_, {stream_id});
}
```

`examples/sumomo/src/sumomo.cpp:645-654` も同一構造で、`audio_result` / `video_result` が代入後に参照されていない。

## 設計方針

本体の既存パターン (`src/sora_signaling.cpp:637-642`) に倣い、`AddTrack` の戻り値を `.ok()` でチェックし、失敗時は `RTC_LOG(LS_ERROR)` でエラーメッセージ (`error().message()`) を出力する。`[[maybe_unused]]` による警告抑制は、エラーを握りつぶす現状の問題を解決しないため採用しない。

`OnSetOffer` の戻り値型は `void` であり、サンプルとしては一方のトラック追加が失敗しても他方の追加は試みるため、エラーログ出力のみを行い処理は継続する (切断や `return` はしない)。ログメッセージは英語で記述する。

参考にする本体の既存パターン (`src/sora_signaling.cpp:637-642`):

```cpp
webrtc::RTCErrorOr<webrtc::scoped_refptr<webrtc::PeerConnectionInterface>>
    connection = config_.pc_factory->CreatePeerConnectionOrError(
        rtc_config, std::move(dependencies));
if (!connection.ok()) {
  RTC_LOG(LS_ERROR) << "CreatePeerConnection failed: error="
                    << connection.error().message();
  return nullptr;
}
```

実装イメージ (sdl_sample / sumomo 共通、audio 側の例):

```cpp
if (audio_track_ != nullptr) {
  webrtc::RTCErrorOr<webrtc::scoped_refptr<webrtc::RtpSenderInterface>>
      audio_result =
          conn_->GetPeerConnection()->AddTrack(audio_track_, {stream_id});
  if (!audio_result.ok()) {
    RTC_LOG(LS_ERROR) << "Failed to add audio track: error="
                      << audio_result.error().message();
  }
}
```

## 完了条件

- 以下の 2 ファイルで `AddTrack` の戻り値が `.ok()` でチェックされ、失敗時に `RTC_LOG(LS_ERROR)` が出力されること:
  - `examples/sumomo/src/sumomo.cpp`
  - `examples/sdl_sample/src/sdl_sample.cpp`
- 変更した両サンプルがビルドできること (例: `python3 examples/sumomo/run.py build macos_arm64` と `python3 examples/sdl_sample/run.py build macos_arm64`)
- `CHANGES.md` の `## develop` 配下、`### misc` セクションに `[FIX]` エントリを追記する:
  ```
  - [FIX] sumomo と sdl_sample で AddTrack の戻り値チェックを追加する
    - @<担当者>
  ```

## 解決方法

- `examples/sumomo/src/sumomo.cpp` と `examples/sdl_sample/src/sdl_sample.cpp` の `OnSetOffer` 内で、
  `AddTrack` の戻り値 `RTCErrorOr` を `.ok()` でチェックし、失敗時に `RTC_LOG(LS_ERROR)` でエラーメッセージを出力するように修正した
- `CHANGES.md` の `## develop` に `[FIX]` エントリを追加した
