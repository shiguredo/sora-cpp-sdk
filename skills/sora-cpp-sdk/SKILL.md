---
name: sora-cpp-sdk
description: WebRTC SFU Sora 向け C++ SDK (Sora C++ SDK) を使ったクライアントアプリの実装。SoraClientContext / SoraSignaling / SoraSignalingObserver の使い方、CMake での組み込み、映像・音声の送受信、データチャネルメッセージング、HWA コーデック選択 (VideoCodecPreference) に関する質問で使用。
---

# Sora C++ SDK

- **対象バージョン**: 2026.2.1 (安定版)
- **libwebrtc**: m150.7871.3.1 ([shiguredo-webrtc-build](https://github.com/shiguredo/webrtc-build) のビルド済みバイナリ)
- **Boost**: 1.92.0
- **リポジトリ**: https://github.com/shiguredo/sora-cpp-sdk
- **リリースバイナリ**: https://github.com/shiguredo/sora-cpp-sdk/releases
- **対応 Sora**: WebRTC SFU Sora 2025.1.0 以降
- **ライセンス**: Apache License 2.0

様々なプラットフォームに対応した [WebRTC SFU Sora](https://sora.shiguredo.jp/) 向けの C++ SDK。
libwebrtc ([shiguredo-webrtc-build](https://github.com/shiguredo/webrtc-build) のビルド済みバイナリ) と Boost の上に、Sora のシグナリングと HWA (ハードウェアアクセラレーション) 対応のエンコーダ / デコーダを提供する。

ドキュメントは存在しない。`examples/` のサンプル集 (sumomo / sdl_sample / messaging_recvonly_sample) が一次資料。質問・相談は時雨堂の Discord (https://discord.gg/shiguredo) のみ。

## 対応プラットフォーム

- Windows 10.1809 x86_64 以降
- macOS 14 arm64 以降
- Ubuntu 22.04 / 24.04 / 26.04 (x86_64, arm64)
- Android 7 arm64 以降 / iOS 14 arm64 以降
- Raspberry Pi OS bookworm (64bit)
- NVIDIA Jetson (JetPack 6, Ubuntu 22.04 / 24.04 ARMv8)

### HWA 対応

| 実装 | `VideoCodecImplementation` | コーデック |
|---|---|---|
| ソフトウェア (libwebrtc 内蔵) | `kInternal` | VP8 / VP9 / AV1 |
| Cisco OpenH264 | `kCiscoOpenH264` | H.264 |
| Intel VPL | `kIntelVpl` | VP9 / AV1 / H.264 / H.265 |
| NVIDIA Video Codec | `kNvidiaVideoCodec` | VP8 / VP9 (デコードのみ) / AV1 / H.264 / H.265 |
| AMD AMF (非推奨) | `kAmdAmf` | VP8 / VP9 (デコードのみ) / AV1 / H.264 / H.265 |
| Raspberry Pi V4L2 M2M | `kRaspiV4L2M2M` | H.264 |
| カスタム実装 | `kCustom_1` 〜 `kCustom_9` | 任意 |

Apple Video Toolbox (H.264 / H.265) と Android HWA は libwebrtc 側の実装を利用する。
AMD AMF はドライバーが不安定なため現在非推奨。

## 導入

### バージョンの組み合わせ

SDK・libwebrtc・Boost のバージョンは厳密に一致させる必要がある。2026.2.1 の組み合わせは以下のとおり。

- `SORA_CPP_SDK_VERSION=2026.2.1`
- `WEBRTC_BUILD_VERSION=m150.7871.3.1`
- `BOOST_VERSION=1.92.0`

別のバージョンを使う場合は、対応するタグの `examples/DEPS` を確認する。

### CMake での組み込み

リリースバイナリを展開して `find_package` で読み込む。`examples/sumomo/CMakeLists.txt` が実例。

```cmake
list(APPEND CMAKE_PREFIX_PATH ${SORA_DIR})
list(APPEND CMAKE_MODULE_PATH ${SORA_DIR}/share/cmake)

set(Boost_USE_STATIC_LIBS ON)
find_package(Boost REQUIRED COMPONENTS json filesystem)
find_package(WebRTC REQUIRED)   # WEBRTC_INCLUDE_DIR / WEBRTC_LIBRARY_DIR を指定
find_package(Sora REQUIRED)

add_executable(myapp src/myapp.cpp)
set_target_properties(myapp PROPERTIES CXX_STANDARD 20 C_STANDARD 20)
target_link_libraries(myapp PRIVATE Sora::sora)
```

プラットフォーム別の必須設定:

- **非 Windows**: libwebrtc 同梱の libc++ を使う。`-nostdinc++ -isystem${LIBCXX_INCLUDE_DIR}` を指定する
- **Windows**: 静的ランタイム `MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"` と `/utf-8 /bigobj` を指定する
- **Linux**: `BUILD_RPATH "$ORIGIN"` を推奨

依存の取得からビルドまでの流れは `examples/sumomo/run.py` を参考にする。サンプル自体は `python3 examples/sumomo/run.py build <target>` でビルドできる (target は `macos_arm64` / `ubuntu-24.04_x86_64` / `windows_x86_64` など)。ローカルビルドの SDK を使う場合は `--local-sora-cpp-sdk-dir` を指定する。

SDK 本体のビルドは複雑なため、基本的にはリリースバイナリを使う。ビルド手順は GitHub Actions の `build.yml` が正となる。

## 基本構造

利用者が扱うクラスは 3 つ。

1. **`sora::SoraClientContext`** (`sora/sora_client_context.h`) — スレッド (network / worker / signaling) と `PeerConnectionFactory` を保持する。プロセスで 1 つ作って使い回す
2. **`sora::SoraSignaling`** (`sora/sora_signaling.h`) — Sora とのシグナリング接続。`Create()` → `Connect()` → `Disconnect()`
3. **`sora::SoraSignalingObserver`** — イベントを受け取るインターフェース。利用者が実装する

イベントループは Boost.Asio の `io_context` を利用者が用意して駆動する。

### 最小構成 (recvonly)

```cpp
#include <sora/sora_client_context.h>
#include <sora/sora_signaling.h>

class MyClient : public std::enable_shared_from_this<MyClient>,
                 public sora::SoraSignalingObserver {
 public:
  MyClient(std::shared_ptr<sora::SoraClientContext> context)
      : context_(context) {}

  void Run() {
    ioc_.reset(new boost::asio::io_context(1));

    sora::SoraSignalingConfig config;
    config.pc_factory = context_->peer_connection_factory();
    config.io_context = ioc_.get();
    config.observer = shared_from_this();  // weak_ptr で保持される
    config.signaling_urls.push_back("wss://sora.example.com/signaling");
    config.channel_id = "sora";
    config.role = "recvonly";
    conn_ = sora::SoraSignaling::Create(config);

    boost::asio::executor_work_guard<boost::asio::io_context::executor_type>
        work_guard(ioc_->get_executor());

    // SIGINT / SIGTERM で graceful に切断する
    boost::asio::signal_set signals(*ioc_, SIGINT, SIGTERM);
    signals.async_wait(
        [this](const boost::system::error_code&, int) { conn_->Disconnect(); });

    conn_->Connect();
    ioc_->run();
  }

  // 純粋仮想関数は全て実装が必要
  void OnSetOffer(std::string offer) override {}
  void OnDisconnect(sora::SoraSignalingErrorCode ec,
                    std::string message) override {
    ioc_->stop();  // 切断されたらイベントループを止める
  }
  void OnNotify(std::string text) override {}
  void OnPush(std::string text) override {}
  void OnMessage(std::string label, std::string data) override {}
  void OnTrack(webrtc::scoped_refptr<webrtc::RtpTransceiverInterface>
                   transceiver) override {}
  void OnRemoveTrack(webrtc::scoped_refptr<webrtc::RtpReceiverInterface>
                         receiver) override {}
  void OnDataChannel(std::string label) override {}

 private:
  std::shared_ptr<sora::SoraClientContext> context_;
  std::shared_ptr<sora::SoraSignaling> conn_;
  std::unique_ptr<boost::asio::io_context> ioc_;
};

int main() {
  sora::SoraClientContextConfig context_config;
  context_config.use_audio_device = false;  // 音声デバイスを使わない場合
  auto context = sora::SoraClientContext::Create(context_config);

  auto client = std::make_shared<MyClient>(context);
  client->Run();
}
```

注意点:

- Observer は `std::weak_ptr` で渡すため、`std::enable_shared_from_this` + `shared_from_this()` を使い、`std::make_shared` で生成する
- Windows では `main` の先頭で `webrtc::ScopedCOMInitializer com_initializer(webrtc::ScopedCOMInitializer::kMTA);` が必要
- `SoraClientContextConfig::use_audio_device = false` にすると音声デバイスを一切掴まない。recvonly やメッセージングのみの用途で推奨

## 映像・音声の送信

トラックを作成し、**`OnSetOffer` のタイミングで `AddTrack`** する。

```cpp
// 事前 (Run 内): ソースとトラックの作成
sora::CameraDeviceCapturerConfig cam_config;
cam_config.width = 640;
cam_config.height = 480;
cam_config.fps = 30;
cam_config.device_name = "";  // 空ならデフォルトデバイス
auto video_source = sora::CreateCameraDeviceCapturer(cam_config);
// カメラが存在しない場合は nullptr が返る

audio_source_ = context_->peer_connection_factory()->CreateAudioSource(
    webrtc::AudioOptions());
audio_track_ = context_->peer_connection_factory()->CreateAudioTrack(
    webrtc::CreateRandomString(16), audio_source_.get());
video_track_ = context_->peer_connection_factory()->CreateVideoTrack(
    video_source, webrtc::CreateRandomString(16));

// OnSetOffer で AddTrack する
void OnSetOffer(std::string offer) override {
  std::string stream_id = webrtc::CreateRandomString(16);
  if (audio_track_) {
    auto result =
        conn_->GetPeerConnection()->AddTrack(audio_track_, {stream_id});
  }
  if (video_track_) {
    auto result =
        conn_->GetPeerConnection()->AddTrack(video_track_, {stream_id});
  }
}
```

- カスタム映像ソースは `sora::ScalableVideoTrackSource` (`sora/scalable_track_source.h`) を継承して作る
- Raspberry Pi Camera を使う場合は `cam_config.use_libcamera = true` にし、実行ファイルと同じディレクトリに `libcamera.so` を置く。`libcamera_controls` に `{"AfMode", "Continuous"}` のようなキーと値のペアを指定できる

## 映像の受信

`OnTrack` で受信トラックが通知される。`webrtc::VideoTrackInterface` に sink (`webrtc::VideoSinkInterface<webrtc::VideoFrame>`) を追加してフレームを受け取る。

```cpp
void OnTrack(webrtc::scoped_refptr<webrtc::RtpTransceiverInterface>
                 transceiver) override {
  auto track = transceiver->receiver()->track();
  if (track->kind() == webrtc::MediaStreamTrackInterface::kVideoKind) {
    renderer_->AddTrack(static_cast<webrtc::VideoTrackInterface*>(track.get()));
  }
}
```

SDL3 での描画例は `examples/sumomo/src/sdl_renderer.cpp` を参照。

## データチャネルメッセージング

`SoraSignalingConfig::data_channels` にラベルと方向を設定して接続する。

```cpp
sora::SoraSignalingConfig::DataChannel dc;
dc.label = "#example";       // ラベルは # で始める
dc.direction = "sendrecv";   // sendonly / recvonly / sendrecv
dc.ordered = true;           // 任意 (optional)
dc.compress = true;          // 任意 (optional)
config.data_channels.push_back(dc);
config.role = "sendrecv";
```

- 送信: `conn_->SendDataChannel(label, data)` (bool を返す。`OnDataChannel(label)` で利用可能になってから送る)
- 受信: `OnMessage(label, data)` で通知される

メッセージングのみの用途 (映像・音声なし) は `config.video = false; config.audio = false;` にする。実例は `examples/messaging_recvonly_sample/` を参照。

## SoraSignalingConfig の主要フィールド

必須は `io_context` / `pc_factory` / `observer` / `signaling_urls` / `channel_id` / `role`。

| フィールド | 説明 |
|---|---|
| `role` | `"sendonly"` / `"recvonly"` / `"sendrecv"` (デフォルト `"sendonly"`) |
| `video`, `audio` | メディアの有効・無効 (デフォルト true) |
| `video_codec_type` | `"VP8"` / `"VP9"` / `"AV1"` / `"H264"` / `"H265"` |
| `video_bit_rate`, `audio_bit_rate` | ビットレート (kbps) |
| `video_h264_params` など | コーデック別パラメータ (boost::json::value) |
| `metadata`, `signaling_notify_metadata` | 認証・通知用メタデータ (boost::json::value) |
| `multistream`, `simulcast`, `spotlight` | `std::optional<bool>`。未設定なら Sora 側の設定に従う |
| `data_channel_signaling`, `ignore_disconnect_websocket` | データチャネル経由シグナリングへの切り替え |
| `client_cert`, `client_key`, `ca_cert` | mTLS 用。ファイルパスではなく**中身の文字列**を渡す |
| `proxy_url` ほか | HTTP プロキシ。利用時は `network_manager` と `socket_factory` の設定が必須 |
| `forwarding_filters` | 転送フィルター |
| `degradation_preference`, `cpu_adaptation` | 負荷時の映像品質制御 |
| `insecure` | サーバー証明書の検証をスキップ (開発用) |

proxy 利用時の `network_manager` / `socket_factory` は `SoraClientContext` から取得する (signaling_thread 上で `BlockingCall` する必要がある。`examples/sumomo/src/sumomo.cpp` 参照)。

## SoraSignalingObserver のコールバック

| コールバック | タイミング |
|---|---|
| `OnSetOffer(offer)` | offer の SDP 設定後。**`AddTrack` はここで行う** |
| `OnDisconnect(ec, message)` | 切断時 (必ず 1 回呼ばれる)。`io_context` の停止はここで行う |
| `OnNotify(text)`, `OnPush(text)` | Sora からの notify / push (JSON 文字列) |
| `OnMessage(label, data)` | データチャネルメッセージの受信 |
| `OnTrack(transceiver)`, `OnRemoveTrack(receiver)` | リモートトラックの追加・削除 |
| `OnDataChannel(label)` | データチャネルが利用可能になった |
| `OnSwitched(text)` | シグナリングが WebSocket からデータチャネルへ切り替わった |
| `OnSignalingMessage(type, direction, message)` | 全シグナリングメッセージ (デバッグ用) |
| `OnWsClose(code, message)` | WebSocket のクローズ |

## HWA コーデックの選択

`SoraClientContextConfig::video_codec_factory_config` で制御する。`preference` が未設定 (nullopt) の場合は `kInternal` (ソフトウェア実装) のみ使う。

```cpp
#include <sora/sora_video_codec.h>

// コーデックごとにエンコーダ / デコーダの実装を指定する
sora::VideoCodecPreference preference;
auto& h264 = preference.GetOrAdd(webrtc::kVideoCodecH264);
h264.encoder = sora::VideoCodecImplementation::kNvidiaVideoCodec;
h264.decoder = sora::VideoCodecImplementation::kNvidiaVideoCodec;
context_config.video_codec_factory_config.preference = preference;

// 実装に応じたコンテキストを capability_config に設定する
auto& cap = context_config.video_codec_factory_config.capability_config;
// NVIDIA Video Codec の場合
if (sora::CudaContext::CanCreate()) {
  cap.cuda_context = sora::CudaContext::Create();
}
// AMD AMF の場合
if (sora::AMFContext::CanCreate()) {
  cap.amf_context = sora::AMFContext::Create();
}
// OpenH264 の場合 (実行時に動的ロードするためパスが必要)
cap.openh264_path = "/path/to/libopenh264.so";
```

- 環境で利用可能な実装の一覧は `sora::GetVideoCodecCapability(config)` で取得できる (sumomo の `--show-video-codec-capability` 相当)
- `sora::ValidateVideoCodecPreference()` で preference が capability に対して妥当か検証できる
- カスタムエンコーダ / デコーダは `kCustom_1` 〜 `kCustom_9` を使い、`create_video_encoder` / `create_video_decoder` と `capability_config.get_custom_engines` を設定する
- OpenH264 はライセンス上バイナリを同梱できないため、利用者が Cisco 提供のバイナリを用意して実行時にパスを渡す

## その他のユーティリティ

- `sora/device_list.h` — カメラ・音声デバイスの列挙
- `sora/rtc_stats.h` — `GetStats` 用のコールバックヘルパー
- `sora/audio_output_helper.h` — iOS のオーディオ出力先 (スピーカー / レシーバー) の切り替え
- `sora/java_context.h`, `sora/android/` — Android (JNI) 対応。`SoraClientContextConfig::get_android_application_context` の設定が必要

## ハマりどころ (FAQ 要点)

- **ビルドできない**: 環境問題がほとんど。GitHub Actions の `build.yml` が動く構成の正
- **Jetson Orin でビルドできない**: Ubuntu 22.04 x86_64 でクロスコンパイルする
- **iOS / macOS から H.264 1080p で配信できない**: H.264 のプロファイルレベル ID を 5.2 以上にする (`video_h264_params` か Sora 側の設定)
- **4K@30fps で配信できない**: `SoraVideoEncoderFactoryConfig::force_i420_conversion` を false にすると改善する場合がある
- **NVIDIA + Windows で小さい VP9 映像を受信できない**: NVIDIA の HW デコーダは width / height のいずれかが 128 未満の VP9 をデコードできない
- **Raspberry Pi の H.264 FHD で緑の線が入る**: v4l2m2m は解像度が 16 の倍数である必要がある。1920x1072 などにする
- **Windows で音声送信できない**: 4 ch マイクは非対応。2 ch 以下のマイクを使う

既知の問題は `doc/known_issues.md`、FAQ の全文は `doc/faq.md` を参照。
