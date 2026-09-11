# 同規格・同型番の音声デバイスの判別方法をドキュメントに追加する

- Created: 2026-09-11
- Completed: {YYYY-MM-DD}
- Branch: feature/update-audio-device-identification-doc
- Polished: {YYYY-MM-DD}

## 目的

同一の音声デバイス (同規格・同型番) を複数接続したときに、どのデバイスがどの ID なのかを判断する方法を `doc/faq.md` に追加する。SDK からは `name` と `guid` が同一になるため判断できず、利用者はデバイスを指定した後に音を鳴らして確認するしかない。

## 現状

- ADM の `RecordingDevices` / `PlayoutDevices` で取得できる `name` と `guid` は、同規格・同型番のデバイスでは同一になる
- `include/sora/device_list.h` のデバイス一覧 API からは複数の同型デバイスを区別できない
- Linux では `pactl list source` / `pacmd list card` を使ってデバイスを特定できることが確認されている
  - `pactl list source` で接続した音声デバイスの `name` を特定する (同じ型番でも末尾に `.1` などが付き一意になる)
  - `pacmd list card` の一覧から `source` に `alsa_input` を持つ card を card index 順に抽出する
  - 抽出結果から `name` と一致する card の位置を調べ、ADM の `RecordingDevices` の index と一致させる (先頭 0 番目がデフォルトのため +1 する)
  - `PlayoutDevice` でも `source` に `alsa_output` を含む card のリストで同様に特定できる
- `doc/faq.md` に音声デバイスの判別方法を説明した記述はない
- 確認には Linux (Jetson など) で複数の同型デバイスを接続した環境が必要

## 設計方針

- `doc/faq.md` に、同規格・同型番の音声デバイスを判別して ADM の `SetRecordingDevice` / `SetPlayoutDevice` に指定する手順を追加する
- `pactl` / `pacmd` の出力と `RecordingDevices` / `PlayoutDevices` の index の対応を、Linux の実機で確認したうえで記載する
- デバイスの判別が SDK の API でできない理由 (name / guid の同一性) を記載する

## 完了条件

- `doc/faq.md` に同規格・同型番の音声デバイスの判別方法が追加されていること
- Linux の実機で手順の妥当性が確認されていること

## Pending 理由

- 複数の同型デバイスを接続した Linux (Jetson など) の実機で確認が必要
- `RecordingDevices` / `PlayoutDevices` の index と `pactl` / `pacmd` の出力の対応を、libwebrtc の ADM 実装を踏まえて確認する必要がある
- ドキュメント追加であり、優先度が確定していない

## Pending 解除条件

- 複数の同型デバイスを接続した Linux 実機で手順が確認できる状態になったこと
