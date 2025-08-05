#include "sora/hwenc_netint/netint_video_decoder.h"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <mutex>
#include <vector>

// WebRTC
#include <api/scoped_refptr.h>
#include <api/video/encoded_image.h>
#include <api/video/i420_buffer.h>
#include <api/video/video_codec_type.h>
#include <api/video/video_frame.h>
#include <api/video/video_frame_buffer.h>
#include <api/video_codecs/video_codec.h>
#include <api/video_codecs/video_decoder.h>
#include <modules/video_coding/include/video_codec_interface.h>
#include <modules/video_coding/include/video_error_codes.h>
#include <rtc_base/checks.h>
#include <rtc_base/logging.h>

// Netint
#include <ni_av_codec.h>
#include <ni_device_api.h>
#include <ni_rsrc_api.h>

#include "../netint_context_impl.cpp"
#include "sora/netint_context.h"

namespace sora {

class NetintVideoDecoderImpl : public NetintVideoDecoder {
 public:
  NetintVideoDecoderImpl(std::shared_ptr<NetintContext> context,
                         webrtc::VideoCodecType codec_type);
  ~NetintVideoDecoderImpl() override;

  bool Configure(const Settings& settings) override;
  int32_t Decode(const webrtc::EncodedImage& input_image,
                 bool missing_frames,
                 int64_t render_time_ms) override;
  int32_t RegisterDecodeCompleteCallback(
      webrtc::DecodedImageCallback* callback) override;
  int32_t Release() override;

 private:
  ni_retcode_t ConvertCodecType(webrtc::VideoCodecType type,
                                ni_codec_t& ni_codec);
  bool AllocateDecoderContext();
  void ReleaseDecoderContext();
  int32_t ConfigureDecoder(int width, int height);

 private:
  std::mutex mutex_;
  webrtc::DecodedImageCallback* callback_ = nullptr;

  std::shared_ptr<NetintContext> context_;
  webrtc::VideoCodecType codec_type_;

  // Netint decoder context
  ni_session_context_t decoder_ctx_ = {};
  ni_session_data_io_t in_packet_ = {};
  ni_session_data_io_t out_frame_ = {};

  bool initialized_ = false;
  int width_ = 0;
  int height_ = 0;

  // Frame buffer pool
  std::vector<rtc::scoped_refptr<webrtc::I420Buffer>> buffer_pool_;
  size_t buffer_pool_size_ = 10;
};

NetintVideoDecoderImpl::NetintVideoDecoderImpl(
    std::shared_ptr<NetintContext> context,
    webrtc::VideoCodecType codec_type)
    : context_(context), codec_type_(codec_type) {}

NetintVideoDecoderImpl::~NetintVideoDecoderImpl() {
  Release();
}

bool NetintVideoDecoderImpl::Configure(const Settings& settings) {
  std::lock_guard<std::mutex> lock(mutex_);

  if (initialized_) {
    RTC_LOG(LS_WARNING) << "Already initialized";
    return true;
  }

  // コーデックタイプを確認
  if (settings.codec_type() != codec_type_) {
    RTC_LOG(LS_ERROR) << "Codec type mismatch";
    return false;
  }

  // デコーダーコンテキストを割り当て
  if (!AllocateDecoderContext()) {
    RTC_LOG(LS_ERROR) << "Failed to allocate decoder context";
    return false;
  }

  initialized_ = true;
  return true;
}

int32_t NetintVideoDecoderImpl::Decode(const webrtc::EncodedImage& input_image,
                                       bool missing_frames,
                                       int64_t render_time_ms) {
  std::lock_guard<std::mutex> lock(mutex_);

  if (!initialized_ || !callback_) {
    RTC_LOG(LS_ERROR) << "Decoder not initialized or no callback registered";
    return WEBRTC_VIDEO_CODEC_UNINITIALIZED;
  }

  // 入力パケットを設定
  in_packet_.data.packet.p_data = const_cast<uint8_t*>(input_image.data());
  in_packet_.data.packet.data_len = input_image.size();
  in_packet_.data.packet.pts = input_image.Timestamp();
  in_packet_.data.packet.dts = input_image.Timestamp();

  // デコーダーに書き込み
  ni_retcode_t ret = ni_device_session_write(&decoder_ctx_, &in_packet_, NI_DEVICE_TYPE_DECODER);
  if (ret != NI_RETCODE_SUCCESS) {
    RTC_LOG(LS_ERROR) << "Failed to write packet to decoder: " << ret;
    return WEBRTC_VIDEO_CODEC_ERROR;
  }

  // デコード結果を読み取り
  ret = ni_device_session_read(&decoder_ctx_, &out_frame_, NI_DEVICE_TYPE_DECODER);
  if (ret == NI_RETCODE_SUCCESS) {
    // フレームデータを取得
    ni_frame_t* ni_frame = &out_frame_.data.frame;

    // 解像度が変更された場合の処理
    if (width_ != ni_frame->video_width || height_ != ni_frame->video_height) {
      width_ = ni_frame->video_width;
      height_ = ni_frame->video_height;

      // バッファプールをクリア
      buffer_pool_.clear();
    }

    // I420バッファを作成
    rtc::scoped_refptr<webrtc::I420Buffer> buffer =
        webrtc::I420Buffer::Create(width_, height_);

    // Y, U, Vプレーンをコピー
    int y_size = width_ * height_;
    int uv_size = y_size / 4;

    memcpy(buffer->MutableDataY(), ni_frame->p_data[0], y_size);
    memcpy(buffer->MutableDataU(), ni_frame->p_data[1], uv_size);
    memcpy(buffer->MutableDataV(), ni_frame->p_data[2], uv_size);

    // VideoFrameを作成
    webrtc::VideoFrame decoded_frame =
        webrtc::VideoFrame::Builder()
            .set_video_frame_buffer(buffer)
            .set_timestamp_rtp(input_image.Timestamp())
            .set_timestamp_ms(render_time_ms)
            .set_rotation(webrtc::kVideoRotation_0)
            .build();

    // コールバックを呼び出し
    callback_->Decoded(decoded_frame);

  } else if (ret == NI_RETCODE_FAILURE) {
    // データがまだない場合
    return WEBRTC_VIDEO_CODEC_OK;
  } else {
    // エラーの場合
    RTC_LOG(LS_ERROR) << "Failed to read from decoder: " << ret;
    return WEBRTC_VIDEO_CODEC_ERROR;
  }

  return WEBRTC_VIDEO_CODEC_OK;
}

int32_t NetintVideoDecoderImpl::RegisterDecodeCompleteCallback(
    webrtc::DecodedImageCallback* callback) {
  std::lock_guard<std::mutex> lock(mutex_);
  callback_ = callback;
  return WEBRTC_VIDEO_CODEC_OK;
}

int32_t NetintVideoDecoderImpl::Release() {
  std::lock_guard<std::mutex> lock(mutex_);

  if (initialized_) {
    ReleaseDecoderContext();
    initialized_ = false;
  }

  callback_ = nullptr;
  buffer_pool_.clear();
  return WEBRTC_VIDEO_CODEC_OK;
}

ni_retcode_t NetintVideoDecoderImpl::ConvertCodecType(
    webrtc::VideoCodecType type,
    ni_codec_t& ni_codec) {
  switch (type) {
    case webrtc::VideoCodecType::kVideoCodecH264:
      ni_codec = NI_CODEC_FORMAT_H264;
      return NI_RETCODE_SUCCESS;
    case webrtc::VideoCodecType::kVideoCodecH265:
      ni_codec = NI_CODEC_FORMAT_H265;
      return NI_RETCODE_SUCCESS;
    case webrtc::VideoCodecType::kVideoCodecAV1:
      ni_codec = NI_CODEC_FORMAT_AV1;
      return NI_RETCODE_SUCCESS;
    default:
      return NI_RETCODE_INVALID_PARAM;
  }
}

bool NetintVideoDecoderImpl::AllocateDecoderContext() {
  // デコーダーセッションコンテキストを初期化
  ni_retcode_t ret = ni_device_session_context_init(&decoder_ctx_);
  if (ret != NI_RETCODE_SUCCESS) {
    RTC_LOG(LS_ERROR) << "Failed to init decoder context: " << ret;
    return false;
  }

  // デバイスを割り当て
  ret = ni_rsrc_allocate_auto(&decoder_ctx_, NI_DEVICE_TYPE_DECODER);
  if (ret != NI_RETCODE_SUCCESS) {
    RTC_LOG(LS_ERROR) << "Failed to allocate decoder device: " << ret;
    ni_device_session_context_clear(&decoder_ctx_);
    return false;
  }

  // セッションをオープン
  ret = ni_device_session_open(&decoder_ctx_, NI_DEVICE_TYPE_DECODER);
  if (ret != NI_RETCODE_SUCCESS) {
    RTC_LOG(LS_ERROR) << "Failed to open decoder session: " << ret;
    ni_rsrc_free_device_context(&decoder_ctx_);
    ni_device_session_context_clear(&decoder_ctx_);
    return false;
  }

  // コーデックタイプを設定
  ni_codec_t ni_codec;
  if (ConvertCodecType(codec_type_, ni_codec) != NI_RETCODE_SUCCESS) {
    RTC_LOG(LS_ERROR) << "Unsupported codec type";
    ReleaseDecoderContext();
    return false;
  }

  decoder_ctx_.codec_format = ni_codec;
  decoder_ctx_.pix_fmt = NI_PIX_FMT_YUV420P;
  decoder_ctx_.keep_alive_timeout = 10;

  // デコーダーの初期化パラメータはセッションオープン時に設定済み

  // I/Oバッファを割り当て
  ret = ni_packet_buffer_alloc(&in_packet_.data.packet, 1024 * 1024);  // 1MB
  if (ret != NI_RETCODE_SUCCESS) {
    RTC_LOG(LS_ERROR) << "Failed to allocate input packet buffer: " << ret;
    ReleaseDecoderContext();
    return false;
  }

  // 出力フレームバッファは最初のデコード時に割り当てる
  return true;
}

void NetintVideoDecoderImpl::ReleaseDecoderContext() {
  // セッションをフラッシュ
  ni_device_session_flush(&decoder_ctx_, NI_DEVICE_TYPE_DECODER);

  // セッションをクローズ
  ni_device_session_close(&decoder_ctx_, 1, NI_DEVICE_TYPE_DECODER);

  // リソースを解放
  ni_rsrc_free_device_context(&decoder_ctx_);

  // コンテキストをクリア
  ni_device_session_context_clear(&decoder_ctx_);

  // I/Oバッファをクリア
  ni_packet_buffer_free(&in_packet_.data.packet);
  ni_frame_buffer_free(&out_frame_.data.frame);
}

int32_t NetintVideoDecoderImpl::ConfigureDecoder(int width, int height) {
  // デコーダーは自動的にストリームから解像度を検出するため、
  // 特別な設定は不要
  return WEBRTC_VIDEO_CODEC_OK;
}

// static
bool NetintVideoDecoder::IsSupported(std::shared_ptr<NetintContext> context,
                                     webrtc::VideoCodecType codec) {
  if (!context) {
    return false;
  }

  // サポートするコーデックタイプを確認
  ni_device_queue_t* device_queue = nullptr;
  ni_retcode_t ret =
      ni_rsrc_get_available_devices(&device_queue, NI_DEVICE_TYPE_DECODER);

  if (ret != NI_RETCODE_SUCCESS || device_queue == nullptr ||
      device_queue->length == 0) {
    return false;
  }

  bool supported = false;
  ni_device_capability_t capability = {};

  // 最初のデバイスの能力を確認
  if (device_queue->length > 0) {
    ret = ni_rsrc_get_device_capability(&device_queue->device[0], &capability);
    if (ret == NI_RETCODE_SUCCESS) {
      // xcoder_devices 配列からデコーダーを探す
      for (int i = 0; i < capability.xcoder_devices_cnt; i++) {
        ni_hw_capability_t* hw_cap = &capability.xcoder_devices[i];
        if (hw_cap->codec_type == NI_DEVICE_TYPE_DECODER) {
          // codec_format の値に基づいてコーデックを判定
          switch (codec) {
            case webrtc::VideoCodecType::kVideoCodecH264:
              if (hw_cap->codec_format == NI_CODEC_FORMAT_H264) {  // 0
                supported = true;
              }
              break;
            case webrtc::VideoCodecType::kVideoCodecH265:
              if (hw_cap->codec_format == NI_CODEC_FORMAT_H265) {  // 1
                supported = true;
              }
              break;
            case webrtc::VideoCodecType::kVideoCodecAV1:
              if (hw_cap->codec_format == NI_CODEC_FORMAT_AV1) {  // 4
                supported = true;
              }
              break;
            default:
              break;
          }
          if (supported) {
            break;  // サポートされていることが確認できたら終了
          }
        }
      }
    }
  }

  ni_rsrc_free_device_queue(device_queue);
  return supported;
}

// static
std::unique_ptr<NetintVideoDecoder> NetintVideoDecoder::Create(
    std::shared_ptr<NetintContext> context,
    webrtc::VideoCodecType codec) {
  if (!IsSupported(context, codec)) {
    RTC_LOG(LS_ERROR) << "Codec not supported by Netint: "
                      << static_cast<int>(codec);
    return nullptr;
  }

  return std::make_unique<NetintVideoDecoderImpl>(context, codec);
}

}  // namespace sora