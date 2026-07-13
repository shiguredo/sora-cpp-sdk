# DeviceVideoCapturer::Init で CreateDeviceInfo の戻り値未チェック

- Priority: High
- Created: 2026-07-10
- Completed: {YYYY-MM-DD}
- Model: DeepSeek V4 Pro
- Branch: feature/fix-device-video-capturer-null-check
- Polished: 2026-07-10

## 目的

`src/device_video_capturer.cpp` の `DeviceVideoCapturer::Init()` 内で `webrtc::VideoCaptureFactory::CreateDeviceInfo()` の戻り値 `device_info` が nullptr チェックされないまま `device_info->GetDeviceName()` が呼ばれており、`CreateDeviceInfo()` が nullptr を返すと null deref でクラッシュする。同ファイルの `Create()` (122-127 行目)、`LogDeviceInfo()` (159-164 行目)、`GetDeviceIndex()` (181-186 行目) はいずれも `CreateDeviceInfo()` の直後に nullptr チェックとログ出力を行っているため、これに倣って `Init()` にも nullptr チェックを追加する。

## 優先度根拠

`CreateDeviceInfo()` はプラットフォーム依存の実装で、環境やメモリ不足時に nullptr を返しうる。`Init()` はデバイス初期化の基本パスであり、`Create()` の 3 つの分岐 (デバイス名指定・デバイスインデックス指定・全デバイス走査) すべてから呼ばれる。ここで null deref すると回避不能なクラッシュとなるため High。

## 現状

`src/device_video_capturer.cpp:48-58`:

```cpp
std::unique_ptr<webrtc::VideoCaptureModule::DeviceInfo> device_info(
    webrtc::VideoCaptureFactory::CreateDeviceInfo());

char device_name[256];
char unique_name[256];
if (device_info->GetDeviceName(static_cast<uint32_t>(capture_device_index),
                               device_name, sizeof(device_name), unique_name,
                               sizeof(unique_name)) != 0) {
  Destroy();
  return false;
}
```

48-49 行目で取得した `device_info` を nullptr チェックせず、53 行目の `device_info->GetDeviceName()` でデリファレンスしている。さらに同じ `device_info` は 66 行目の `device_info->GetCapability()` でも使われる。したがって 48-49 行目の直後に nullptr チェックを 1 箇所追加すれば、53 行目と 66 行目の両方のデリファレンスをまとめて防げる。

同ファイル `Create()` の 122-127 行目では正しく nullptr チェック済み:

```cpp
std::unique_ptr<webrtc::VideoCaptureModule::DeviceInfo> info(
    webrtc::VideoCaptureFactory::CreateDeviceInfo());
if (!info) {
  RTC_LOG(LS_WARNING) << "Failed to CreateDeviceInfo";
  return nullptr;
}
```

## 設計方針

`CreateDeviceInfo()` 呼び出し (48-49 行目) の直後に `device_info` の nullptr チェックを追加し、null 時は `Create()` と同様に `RTC_LOG(LS_WARNING)` でエラーログを出力して `false` を返す。

この時点では `vcm_` はコンストラクタ (36-38 行目) で nullptr に初期化されたままであり、`Destroy()` (144-152 行目) は先頭の `if (!vcm_) return;` で即 return する no-op となる。同種の nullptr チェックである `Create()` (124-127 行目) も `Destroy()` を呼ばずログ出力と早期リターンのみで済ませているため、これに倣い no-op である `Destroy()` は呼ばずログ出力と `return false` のみとする。

変更後 (48 行目直後):

```cpp
std::unique_ptr<webrtc::VideoCaptureModule::DeviceInfo> device_info(
    webrtc::VideoCaptureFactory::CreateDeviceInfo());
if (!device_info) {
  RTC_LOG(LS_WARNING) << "Failed to CreateDeviceInfo";
  return false;
}
```

`Init()` が `false` を返すと呼び出し元の `Create()` (100-101, 112-113, 131 行目) が nullptr またはループ継続で処理するため、後方互換性への影響はなく、正常系の挙動も変わらない純粋な防御的追加である。

## 完了条件

- `CreateDeviceInfo()` が nullptr を返した場合に null deref せず、`RTC_LOG(LS_WARNING)` でエラーログを出力して `false` を返すこと
- nullptr チェックが `CreateDeviceInfo()` 呼び出しの直後、`device_info->GetDeviceName()` (53 行目) より前に追加されていること (これにより 66 行目の `device_info->GetCapability()` も同時に保護される)
- `CreateDeviceInfo()` の失敗は環境依存であり単体テストでの再現は困難である。`tests/` 配下に `DeviceVideoCapturer` を対象とした既存テストはないため、コードレビューにより nullptr チェックが正しく追加されていることを確認する
- `CHANGES.md` の `## develop` 直下（`### misc` セクションより前）に `[FIX]` エントリを追記する。`### misc` は Examples / CI / tooling 用のため使わない:
  ```
  - [FIX] `DeviceVideoCapturer::Init` で `CreateDeviceInfo` の戻り値未チェックを修正する
    - @<担当者>
  ```
