# DeviceVideoCapturer::Init で CreateDeviceInfo の戻り値未チェック

- Priority: High
- Created: 2026-07-10
- Completed: {YYYY-MM-DD}
- Model: DeepSeek V4 Pro
- Branch: feature/fix-device-video-capturer-null-check
- Polished: {YYYY-MM-DD}

## 目的

`src/device_video_capturer.cpp` の `Init()` 関数内で `CreateDeviceInfo()` が `nullptr` を返しうるのに null チェックがない。同ファイルの `Create()` 関数 (122-127 行目) では正しく null チェックを行っている。

## 優先度根拠

`CreateDeviceInfo()` が失敗した場合に null deref でクラッシュする。デバイス初期化の基本パスであり High。

## 現状

`src/device_video_capturer.cpp:48-55`:

```cpp
std::unique_ptr<webrtc::VideoCaptureModule::DeviceInfo> device_info(
    webrtc::VideoCaptureFactory::CreateDeviceInfo());

char device_name[256];
char unique_name[256];
if (device_info->GetDeviceName(...) != 0) {  // device_info が nullptr だとクラッシュ
```

同ファイル `Create()` の 122-127 行目では正しく null チェック済み:

```cpp
std::unique_ptr<webrtc::VideoCaptureModule::DeviceInfo> info(
    webrtc::VideoCaptureFactory::CreateDeviceInfo());
if (!info) {
    RTC_LOG(LS_WARNING) << "Failed to CreateDeviceInfo";
    return nullptr;
}
```

## 設計方針

`Init()` 内でも `Create()` と同様に `device_info` の null チェックを追加し、null 時は `Destroy()` を呼んで `false` を返す。

## 完了条件

- `CreateDeviceInfo()` が nullptr を返した場合に null deref せずエラーリターンすること
