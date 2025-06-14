# Sora C++ SDK 使用ガイド

## 概要

Sora C++ SDK は、WebRTC を使用したリアルタイム通信を実現するための C++ ライブラリです。このドキュメントでは、SDK の基本的な使い方から高度な機能まで、詳しく解説します。

## 目次

1. [はじめに](#はじめに)
2. [インストールとセットアップ](#インストールとセットアップ)
3. [基本的な使い方](#基本的な使い方)
4. [高度な機能](#高度な機能)
5. [プラットフォーム固有の機能](#プラットフォーム固有の機能)
6. [エラーハンドリング](#エラーハンドリング)
7. [サンプルアプリケーション](#サンプルアプリケーション)
8. [トラブルシューティング](#トラブルシューティング)

## はじめに

Sora C++ SDK は以下の主要な機能を提供します：

- **マルチプラットフォーム対応**: Windows、macOS、Linux、Android、iOS
- **ハードウェアアクセラレーション**: NVIDIA、Intel、AMD の各種ハードウェアエンコーダー対応
- **柔軟なシグナリング**: WebSocket と DataChannel の両方に対応
- **高度な配信機能**: マルチストリーム、サイマルキャスト、スポットライト対応

### 主要なクラス

- `SoraClientContext`: WebRTC のスレッドと PeerConnectionFactory を管理
- `SoraSignaling`: WebRTC のシグナリング処理を担当
- `SoraSignalingObserver`: シグナリングイベントを受け取るインターフェース

## インストールとセットアップ

### 必要な依存関係

- WebRTC ライブラリ
- Boost (JSON、Filesystem、ASIO)
- OpenH264 (オプション)
- プラットフォーム固有の開発ツール

### CMake を使用したビルド

```cmake
# CMakeLists.txt の例
cmake_minimum_required(VERSION 3.16)
project(MyApp)

# Sora C++ SDK の追加
add_subdirectory(sora-cpp-sdk)

# アプリケーションのビルド
add_executable(myapp main.cpp)
target_link_libraries(myapp sora)
```

## 基本的な使い方

### 1. SDK の初期化

```cpp
#include <sora/sora_client_context.h>
#include <sora/sora_signaling.h>

// SoraClientContext の設定と作成
sora::SoraClientContextConfig context_config;
context_config.use_audio_device = true;  // オーディオデバイスを使用

// コンテキストの作成
auto context = sora::SoraClientContext::Create(context_config);
```

### 2. シグナリング接続の確立

```cpp
// boost::asio::io_context の作成
std::unique_ptr<boost::asio::io_context> ioc(new boost::asio::io_context(1));

// SoraSignalingConfig の設定
sora::SoraSignalingConfig config;
config.pc_factory = context->peer_connection_factory();
config.io_context = ioc.get();
config.observer = shared_from_this();  // SoraSignalingObserver を実装
config.signaling_urls.push_back("wss://sora.example.com/signaling");
config.channel_id = "your-channel-id";
config.role = "sendrecv";  // sendonly, recvonly, sendrecv から選択

// ビデオ・オーディオの設定
config.video = true;
config.audio = true;
config.video_codec_type = "VP9";
config.audio_codec_type = "OPUS";

// 接続の作成と開始
auto conn = sora::SoraSignaling::Create(config);
conn->Connect();

// イベントループの実行
ioc->run();
```

### 3. Observer の実装

```cpp
class MySoraClient : public std::enable_shared_from_this<MySoraClient>,
                     public sora::SoraSignalingObserver {
public:
    // Offer を受信した時の処理（必須）
    void OnSetOffer(std::string offer) override {
        // 送信の場合、トラックを追加
        if (role_ != "recvonly") {
            std::string stream_id = webrtc::CreateRandomString(16);
            conn_->GetPeerConnection()->AddTrack(audio_track_, {stream_id});
            conn_->GetPeerConnection()->AddTrack(video_track_, {stream_id});
        }
    }
    
    // 切断時の処理（必須）
    void OnDisconnect(sora::SoraSignalingErrorCode ec, 
                      std::string message) override {
        RTC_LOG(LS_INFO) << "Disconnected: " << message;
        // クリーンアップ処理
    }
    
    // トラック受信時の処理（必須）
    void OnTrack(webrtc::scoped_refptr<webrtc::RtpTransceiverInterface> 
                 transceiver) override {
        auto track = transceiver->receiver()->track();
        if (track->kind() == webrtc::MediaStreamTrackInterface::kVideoKind) {
            // ビデオトラックの処理
            auto video_track = 
                static_cast<webrtc::VideoTrackInterface*>(track.get());
            // レンダラーに追加するなど
        }
    }
    
    // その他の必須コールバック
    void OnRemoveTrack(webrtc::scoped_refptr<webrtc::RtpReceiverInterface> 
                       receiver) override {}
    void OnNotify(std::string text) override {}
    void OnPush(std::string text) override {}
    void OnMessage(std::string label, std::string data) override {}
    void OnDataChannel(std::string label) override {}
};
```

### 4. ビデオキャプチャの設定

```cpp
// カメラデバイスからのキャプチャ
sora::CameraDeviceCapturerConfig cam_config;
cam_config.width = 1280;
cam_config.height = 720;
cam_config.fps = 30;
cam_config.device_name = "HD WebCam";  // 特定のデバイスを指定（オプション）

// キャプチャの作成
auto video_source = sora::CreateCameraDeviceCapturer(cam_config);
if (video_source == nullptr) {
    RTC_LOG(LS_ERROR) << "Failed to create camera capturer";
    return;
}

// ビデオトラックの作成
auto video_track = context->peer_connection_factory()->CreateVideoTrack(
    video_source, "video-track-id");
```

## 高度な機能

### マルチストリーム

複数のストリームを同時に送受信できます。

```cpp
config.multistream = true;
```

### サイマルキャスト

異なる品質の複数のストリームを同時に配信します。

```cpp
config.simulcast = true;

// サイマルキャストのエンコーディング設定
sora::SoraSignalingConfig::SimulcastRid rid;
rid.rid = "r0";
rid.max_bitrate = 3000;  // kbps
rid.max_framerate = 30;
rid.scale_resolution_down_by = 1.0;
config.simulcast_rids.push_back(rid);

rid.rid = "r1";
rid.max_bitrate = 1000;
rid.max_framerate = 30;
rid.scale_resolution_down_by = 2.0;
config.simulcast_rids.push_back(rid);
```

### スポットライト

注目する配信者を動的に切り替える機能です。

```cpp
config.spotlight = true;
config.spotlight_number = 3;  // 同時に配信するストリーム数
config.spotlight_focus_rid = "r0";  // フォーカス時の RID
config.spotlight_unfocus_rid = "r1";  // アンフォーカス時の RID
```

### DataChannel

低遅延のデータ通信を実現します。

```cpp
// DataChannel の設定
sora::SoraSignalingConfig::DataChannel dc;
dc.label = "chat";
dc.direction = "sendrecv";  // sendonly, recvonly, sendrecv
dc.ordered = true;  // 順序保証
dc.compress = false;  // 圧縮の有無
config.data_channels.push_back(dc);

// メッセージの送信
conn->SendDataChannel("chat", "Hello, World!");

// メッセージの受信（Observer 内）
void OnMessage(std::string label, std::string data) override {
    if (label == "chat") {
        std::cout << "Received: " << data << std::endl;
    }
}
```

### 統計情報の取得

```cpp
// 統計情報の取得
conn->GetPeerConnection()->GetStats(
    sora::RTCStatsCallback::Create(
        [](const webrtc::scoped_refptr<const webrtc::RTCStatsReport>& report) {
            // JSON 形式で出力
            std::string json_stats = report->ToJson();
            std::cout << "Stats: " << json_stats << std::endl;
            
            // 特定の統計情報を取得
            for (const auto& stats : *report) {
                if (stats.type() == webrtc::RTCInboundRtpStreamStats::kType) {
                    auto& inbound = 
                        stats.cast_to<webrtc::RTCInboundRtpStreamStats>();
                    if (inbound.bytes_received.is_defined()) {
                        std::cout << "Bytes received: " 
                                  << *inbound.bytes_received << std::endl;
                    }
                }
            }
        })
        .get());
```

## プラットフォーム固有の機能

### ハードウェアエンコーダー

#### NVIDIA GPU (NVCODEC)

```cpp
// Linux/Windows で利用可能
sora::CameraDeviceCapturerConfig cam_config;
cam_config.use_native = true;  // ネイティブキャプチャを使用

// CUDA コンテキストの作成
if (sora::CudaContext::CanCreate()) {
    cam_config.cuda_context = sora::CudaContext::Create();
}

// コーデック設定
context_config.video_codec_factory_config.preference = []() {
    sora::VideoCodecPreference preference;
    auto& h264 = preference.GetOrAdd(webrtc::kVideoCodecH264);
    h264.encoder = sora::VideoCodecImplementation::kNvidiaVideoCodecSdk;
    h264.decoder = sora::VideoCodecImplementation::kNvidiaVideoCodecSdk;
    return preference;
}();
```

#### Intel VPL

```cpp
// Intel VPL の使用
auto& h265 = preference.GetOrAdd(webrtc::kVideoCodecH265);
h265.encoder = sora::VideoCodecImplementation::kIntelVpl;
h265.decoder = sora::VideoCodecImplementation::kIntelVpl;
```

#### AMD AMF

```cpp
// AMD AMF の使用
auto& h264 = preference.GetOrAdd(webrtc::kVideoCodecH264);
h264.encoder = sora::VideoCodecImplementation::kAmdAmf;
h264.decoder = sora::VideoCodecImplementation::kAmdAmf;
```

### Android 固有の設定

```cpp
// Application Context の設定
context_config.get_android_application_context = 
    [](void* env) -> void* {
        // JNI を使用して Application Context を取得
        return GetApplicationContext(env);
    };

// カメラキャプチャの設定
sora::CameraDeviceCapturerConfig cam_config;
cam_config.jni_env = env;
cam_config.application_context = application_context;
cam_config.signaling_thread = context->signaling_thread();
```

### iOS/macOS 固有の設定

```cpp
// iOS のオーディオ設定
#if defined(__APPLE__)
// オーディオセッションの設定が自動的に行われます
#endif
```

## エラーハンドリング

### エラーコードの種類

```cpp
enum class SoraSignalingErrorCode {
  CLOSE_SUCCEEDED,               // 正常終了
  CLOSE_FAILED,                  // クローズ処理失敗
  INTERNAL_ERROR,                // 内部エラー
  INVALID_PARAMETER,             // 無効なパラメータ
  WEBSOCKET_HANDSHAKE_FAILED,    // WebSocketハンドシェイク失敗
  WEBSOCKET_ONCLOSE,             // WebSocketクローズ
  WEBSOCKET_ONERROR,             // WebSocketエラー
  PEER_CONNECTION_STATE_FAILED,  // PeerConnection失敗
  ICE_FAILED,                    // ICE接続失敗
};
```

### エラーハンドリングの実装例

```cpp
void OnDisconnect(sora::SoraSignalingErrorCode ec, 
                  std::string message) override {
    switch (ec) {
        case sora::SoraSignalingErrorCode::CLOSE_SUCCEEDED:
            // 正常終了
            RTC_LOG(LS_INFO) << "Connection closed normally";
            break;
            
        case sora::SoraSignalingErrorCode::PEER_CONNECTION_STATE_FAILED:
        case sora::SoraSignalingErrorCode::ICE_FAILED:
            // ネットワークエラー
            RTC_LOG(LS_ERROR) << "Network error: " << message;
            // 再接続処理を実行
            ScheduleReconnect();
            break;
            
        case sora::SoraSignalingErrorCode::WEBSOCKET_HANDSHAKE_FAILED:
            // 認証エラーの可能性
            RTC_LOG(LS_ERROR) << "Handshake failed: " << message;
            // 認証情報を確認
            break;
            
        default:
            // その他のエラー
            RTC_LOG(LS_ERROR) << "Error occurred: " << message;
            break;
    }
}
```

### 再接続の実装

```cpp
class ReconnectingSoraClient : public sora::SoraSignalingObserver {
private:
    void ScheduleReconnect() {
        reconnect_timer_.expires_from_now(
            boost::posix_time::seconds(reconnect_delay_));
        reconnect_timer_.async_wait(
            [this](boost::system::error_code ec) {
                if (!ec) {
                    // 指数バックオフ
                    reconnect_delay_ = std::min(
                        reconnect_delay_ * 2, max_reconnect_delay_);
                    conn_->Connect();
                }
            });
    }
    
    int reconnect_delay_ = 1;
    const int max_reconnect_delay_ = 60;
    boost::asio::deadline_timer reconnect_timer_;
};
```

### タイムアウトの設定

```cpp
// WebSocket 接続タイムアウト
config.websocket_connection_timeout = 30;  // 秒

// WebSocket クローズタイムアウト
config.websocket_close_timeout = 3;  // 秒

// DataChannel シグナリングタイムアウト
config.data_channel_signaling_timeout = 180;  // 秒
```

## サンプルアプリケーション

SDK には以下のサンプルアプリケーションが含まれています：

### sumomo

フル機能のサンプルクライアントです。

```bash
# ビルド
cd examples/sumomo
mkdir build && cd build
cmake ..
make

# 実行例（送信）
./sumomo --signaling-url wss://sora.example.com/signaling \
         --channel-id test-channel \
         --role sendonly \
         --video-codec-type VP9 \
         --video-bit-rate 2000
```

### SDL サンプル

SDL を使用した映像表示のサンプルです。

```bash
# 実行例（受信）
./sdl_sample --signaling-url wss://sora.example.com/signaling \
             --channel-id test-channel \
             --role recvonly
```

### メッセージング受信専用サンプル

最小構成の受信専用サンプルです。

## トラブルシューティング

### カメラが検出されない

```cpp
// 利用可能なカメラデバイスを列挙
sora::DeviceList::EnumVideoCapturer(
    [](std::string device_name, std::string unique_name) {
        std::cout << "Video device: " << device_name 
                  << " (" << unique_name << ")" << std::endl;
    },
    nullptr  // Android の場合は application_context
);
```

### 音声が聞こえない

```cpp
// オーディオデバイスの確認
sora::DeviceList::EnumAudioPlayout(
    [](std::string device_name, std::string unique_name) {
        std::cout << "Audio device: " << device_name << std::endl;
    }
);

// 特定のデバイスを指定
context_config.audio_playout_device = "スピーカー名";
```

### 接続が確立できない

1. シグナリング URL が正しいか確認
2. ネットワーク接続を確認
3. ファイアウォール設定を確認
4. 証明書設定を確認（SSL接続の場合）

```cpp
// 証明書検証をスキップ（開発環境のみ）
config.insecure = true;

// プロキシ経由の接続
config.proxy_url = "http://proxy.example.com:8080";
config.proxy_username = "user";
config.proxy_password = "password";
```

### パフォーマンス問題

```cpp
// ビットレートの調整
config.video_bit_rate = 1000;  // kbps

// 解像度の調整
cam_config.width = 640;
cam_config.height = 480;

// フレームレートの調整
cam_config.fps = 15;

// ハードウェアエンコーダーの使用
cam_config.use_native = true;
```

## まとめ

Sora C++ SDK は、WebRTC を使用したリアルタイム通信を簡単に実装できる強力なツールです。基本的な送受信から、マルチストリーム、サイマルキャスト、ハードウェアエンコーディングまで、幅広い機能を提供しています。

このドキュメントで説明した内容を参考に、用途に応じたアプリケーションを開発してください。より詳細な情報については、SDK のヘッダーファイルやサンプルコードを参照することをお勧めします。