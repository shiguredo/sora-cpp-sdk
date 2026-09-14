# iPad の外部 USB カメラに対応する

- Created: 2026-09-10
- Completed: YYYY-MM-DD
- Branch: feature/add-ipad-external-camera
- Polished: 2026-09-15

## 目的

iPadOS 17.0 以降の iPad では USB 接続の UVC 外部カメラが利用できる。 Sora C++ SDK の iOS ビルドでも外部カメラを列挙し、 `CameraDeviceCapturerConfig::device_name` で選択して配信できるようにする。

Sora Unity SDK の iOS ビルドは `sora::MacCapturer::EnumVideoDevice` と `sora::CreateCameraDeviceCapturer` を利用しているため、 本対応により Unity SDK からも外部カメラを利用できるようになる。

## 現状

- `src/mac/mac_capturer.mm` の `captureDevices()` は `SORA_CPP_SDK_MACOS` のときだけ `AVCaptureDeviceTypeExternal` を含めている。 iOS ビルド (`SORA_CPP_SDK_IOS`) では `AVCaptureDeviceTypeBuiltInWideAngleCamera` のみを列挙し、 外部カメラが含まれない
- `DeviceList::EnumVideoCapturer` (`src/device_list.cpp`) は iOS / macOS で `MacCapturer::EnumVideoDevice` を呼び、 `MacCapturer::FindVideoDevice` も同じ `captureDevices()` を利用する。 iOS では外部カメラの列挙も `device_name` / uniqueID による選択もできない
- `CreateCameraDeviceCapturer` (`src/camera_device_capturer.cpp`) が受け取る `CameraDeviceCapturerConfig` には、 デバイス指定に `device_name` しかない。 `MacCapturerConfig::device` (`include/sora/mac/mac_capturer.h`) で `MacCapturer::Create` に `AVCaptureDevice*` を直接渡すことはできるが、 `CreateCameraDeviceCapturer` の経路では利用できない
- iOS の deployment target は 14.0 である。 `AVCaptureDevice.DeviceType.external` は iOS 17.0 以降でのみ利用できるため、 macOS の分岐と同じように無条件で追加することはできない
- macOS ビルドは `AVCaptureDeviceTypeExternal` を含めており外部カメラに対応済みである
- libwebrtc の `RTCCameraVideoCapturer.captureDevices()` は組み込み広角カメラのみを列挙する (M154 時点) ため、 SDK 側の `captureDevices()` で対応する必要がある

## 設計方針

- `src/mac/mac_capturer.mm` の `captureDevices()` を変更し、 iOS では `@available(iOS 17.0, *)` が真のときだけ `AVCaptureDeviceTypeExternal` を追加する。 macOS の分岐 (`SORA_CPP_SDK_MACOS`) は現状を維持する
  - 既存アプリがデバイス番号で指定している場合に番号が変わらないよう、 組み込みカメラの後ろへ外部カメラを連結する
  - `EnumVideoDevice` と `FindVideoDevice` は同じ `captureDevices()` を利用するため、 列挙と `device_name` / uniqueID による選択の両方が外部カメラに対応する
- 外部カメラの `AVCaptureDevice.position` は `.unspecified` になるが、 `MacCapturer` は position を参照しないため変更は不要
- `CameraDeviceCapturerConfig` / `MacCapturerConfig` の公開 API は変更しない。 既存の `device_name` 検索 (番号 / 名前前方一致 / uniqueID 完全一致) をそのまま外部カメラにも適用する
- examples は iOS に対応していないため、 サンプルの変更は行わない
- 変更履歴 (`CHANGES.md`) の `## develop` に `[ADD]` のエントリを追記する

## テスト方針

- 実機 iPad (iPadOS 17.0 以降) に USB 外部カメラを接続し、 `DeviceList::EnumVideoCapturer` に外部カメラが列挙されることを確認する
- 外部カメラの `device_name` / uniqueID を指定して `CreateCameraDeviceCapturer` でキャプチャラを生成し、 外部カメラの映像が送信されることを確認する
- 外部カメラ未接続の状態と iOS 16 以前で、 従来どおり組み込みカメラのみが列挙されることを確認する
- macOS の列挙と選択に回帰がないことを確認する
- iOS の外部カメラは CI で検証できないため、 実機での確認結果を issue に記録する

## 完了条件

- iPadOS 17.0 以降の iPad に外部カメラを接続したとき、 `DeviceList::EnumVideoCapturer` の結果に外部カメラが含まれること
- `CameraDeviceCapturerConfig::device_name` に外部カメラの device_name または uniqueID を指定すると、 外部カメラのキャプチャラが生成され映像が送信されること
- iOS 16 以前と macOS のデバイス列挙・選択の挙動が変わらないこと
- 変更履歴 (`CHANGES.md`) の `## develop` にエントリを追記すること

## 変更対象ファイル

- `src/mac/mac_capturer.mm`
- 必要に応じて `include/sora/mac/mac_capturer.h`
- `CHANGES.md`

## 解決方法
