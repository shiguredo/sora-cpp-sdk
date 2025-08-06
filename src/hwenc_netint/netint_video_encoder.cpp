#include "sora/hwenc_netint/netint_video_encoder.h"

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
#include <api/video/video_codec_type.h>
#include <api/video/video_content_type.h>
#include <api/video/video_frame.h>
#include <api/video/video_frame_buffer.h>
#include <api/video/video_frame_type.h>
#include <api/video/video_timing.h>
#include <api/video_codecs/video_codec.h>
#include <api/video_codecs/video_encoder.h>
#include <common_video/h264/h264_bitstream_parser.h>
#include <common_video/h265/h265_bitstream_parser.h>
#include <common_video/include/bitrate_adjuster.h>
#include <modules/video_coding/codecs/h264/include/h264_globals.h>
#include <modules/video_coding/codecs/interface/common_constants.h>
#include <modules/video_coding/include/video_codec_interface.h>
#include <modules/video_coding/include/video_error_codes.h>
#include <rtc_base/checks.h>
#include <rtc_base/logging.h>

// libyuv
#include <libyuv/convert_from.h>

// Netint
#include <ni_av_codec.h>
#include <ni_device_api.h>
#include <ni_rsrc_api.h>

#include "../netint_context_impl.cpp"
#include "sora/netint_context.h"

namespace sora {

class NetintVideoEncoderImpl : public NetintVideoEncoder {
 public:
  NetintVideoEncoderImpl(std::shared_ptr<NetintContext> context,
                         webrtc::VideoCodecType codec_type);
  ~NetintVideoEncoderImpl() override;

  int32_t InitEncode(const webrtc::VideoCodec* codec_settings,
                     int32_t number_of_cores,
                     size_t max_payload_size) override;
  int32_t RegisterEncodeCompleteCallback(
      webrtc::EncodedImageCallback* callback) override;
  int32_t Release() override;
  int32_t Encode(
      const webrtc::VideoFrame& frame,
      const std::vector<webrtc::VideoFrameType>* frame_types) override;
  void SetRates(const RateControlParameters& parameters) override;
  webrtc::VideoEncoder::EncoderInfo GetEncoderInfo() const override;

 private:
  ni_retcode_t ConvertCodecType(webrtc::VideoCodecType type,
                                ni_codec_t& ni_codec);
  bool AllocateEncoderContext();
  void ReleaseEncoderContext();
  int32_t ConfigureEncoder(const webrtc::VideoCodec* codec_settings);

 private:
  std::mutex mutex_;
  webrtc::EncodedImageCallback* callback_ = nullptr;
  webrtc::BitrateAdjuster bitrate_adjuster_;

  std::shared_ptr<NetintContext> context_;
  webrtc::VideoCodecType codec_type_;

  // Netint encoder context
  ni_session_context_t encoder_ctx_ = {};
  ni_session_data_io_t in_frame_ = {};
  ni_session_data_io_t out_packet_ = {};

  bool initialized_ = false;
  int width_ = 0;
  int height_ = 0;
  int framerate_ = 0;
  uint32_t target_bitrate_bps_ = 0;
  uint32_t max_bitrate_bps_ = 0;

  // H264/H265 bitstream parsers
  std::unique_ptr<webrtc::H264BitstreamParser> h264_bitstream_parser_;
  std::unique_ptr<webrtc::H265BitstreamParser> h265_bitstream_parser_;

  // Frame buffer for conversion
  std::vector<uint8_t> conversion_buffer_;
};

NetintVideoEncoderImpl::NetintVideoEncoderImpl(
    std::shared_ptr<NetintContext> context,
    webrtc::VideoCodecType codec_type)
    : context_(context), codec_type_(codec_type), bitrate_adjuster_(0.5, 0.95) {
  if (codec_type == webrtc::VideoCodecType::kVideoCodecH264) {
    h264_bitstream_parser_ = std::make_unique<webrtc::H264BitstreamParser>();
  } else if (codec_type == webrtc::VideoCodecType::kVideoCodecH265) {
    h265_bitstream_parser_ = std::make_unique<webrtc::H265BitstreamParser>();
  }
}

NetintVideoEncoderImpl::~NetintVideoEncoderImpl() {
  Release();
}

int32_t NetintVideoEncoderImpl::InitEncode(
    const webrtc::VideoCodec* codec_settings,
    int32_t number_of_cores,
    size_t max_payload_size) {
  RTC_DCHECK(codec_settings);
  RTC_DCHECK_EQ(codec_settings->codecType, codec_type_);

  std::lock_guard<std::mutex> lock(mutex_);

  if (initialized_) {
    RTC_LOG(LS_WARNING) << "Already initialized";
    return WEBRTC_VIDEO_CODEC_OK;
  }

  width_ = codec_settings->width;
  height_ = codec_settings->height;
  framerate_ = codec_settings->maxFramerate;
  target_bitrate_bps_ = codec_settings->startBitrate * 1000;
  max_bitrate_bps_ = codec_settings->maxBitrate * 1000;

  // ビットレート調整器を初期化
  bitrate_adjuster_.SetTargetBitrateBps(target_bitrate_bps_);

  // エンコーダーコンテキストを割り当て
  if (!AllocateEncoderContext()) {
    RTC_LOG(LS_ERROR) << "Failed to allocate encoder context";
    return WEBRTC_VIDEO_CODEC_ERROR;
  }

  // エンコーダーを設定
  int32_t ret = ConfigureEncoder(codec_settings);
  if (ret != WEBRTC_VIDEO_CODEC_OK) {
    ReleaseEncoderContext();
    return ret;
  }

  initialized_ = true;
  return WEBRTC_VIDEO_CODEC_OK;
}

int32_t NetintVideoEncoderImpl::RegisterEncodeCompleteCallback(
    webrtc::EncodedImageCallback* callback) {
  std::lock_guard<std::mutex> lock(mutex_);
  callback_ = callback;
  return WEBRTC_VIDEO_CODEC_OK;
}

int32_t NetintVideoEncoderImpl::Release() {
  std::lock_guard<std::mutex> lock(mutex_);

  if (initialized_) {
    ReleaseEncoderContext();
    initialized_ = false;
  }

  callback_ = nullptr;
  return WEBRTC_VIDEO_CODEC_OK;
}

int32_t NetintVideoEncoderImpl::Encode(
    const webrtc::VideoFrame& frame,
    const std::vector<webrtc::VideoFrameType>* frame_types) {
  std::lock_guard<std::mutex> lock(mutex_);

  if (!initialized_ || !callback_) {
    return WEBRTC_VIDEO_CODEC_UNINITIALIZED;
  }

  // キーフレームのリクエストを確認
  bool force_key_frame = false;
  if (frame_types != nullptr) {
    for (auto frame_type : *frame_types) {
      if (frame_type == webrtc::VideoFrameType::kVideoFrameKey) {
        force_key_frame = true;
        break;
      }
    }
  }

  // フレームをNI形式に変換
  webrtc::scoped_refptr<webrtc::I420BufferInterface> i420_buffer =
      frame.video_frame_buffer()->ToI420();

  // Netint エンコーダーに入力データを設定
  in_frame_.data.frame.video_width = i420_buffer->width();
  in_frame_.data.frame.video_height = i420_buffer->height();
  in_frame_.data.frame.p_data[0] = const_cast<uint8_t*>(i420_buffer->DataY());
  in_frame_.data.frame.p_data[1] = const_cast<uint8_t*>(i420_buffer->DataU());
  in_frame_.data.frame.p_data[2] = const_cast<uint8_t*>(i420_buffer->DataV());
  in_frame_.data.frame.data_len[0] =
      i420_buffer->StrideY() * i420_buffer->height();
  in_frame_.data.frame.data_len[1] =
      i420_buffer->StrideU() * i420_buffer->ChromaHeight();
  in_frame_.data.frame.data_len[2] =
      i420_buffer->StrideV() * i420_buffer->ChromaHeight();
  in_frame_.data.frame.pts = frame.timestamp_us();
  in_frame_.data.frame.dts = frame.timestamp_us();
  in_frame_.data.frame.force_key_frame = force_key_frame ? 1 : 0;

  // エンコード実行
  int ret = ni_device_session_write(&encoder_ctx_, &in_frame_,
                                    NI_DEVICE_TYPE_ENCODER);
  if (ret != NI_RETCODE_SUCCESS) {
    RTC_LOG(LS_ERROR) << "Failed to write frame to encoder: " << ret;
    return WEBRTC_VIDEO_CODEC_ERROR;
  }

  // エンコード結果を読み取り
  ret = ni_device_session_read(&encoder_ctx_, &out_packet_,
                               NI_DEVICE_TYPE_ENCODER);
  if (ret == NI_RETCODE_SUCCESS) {
    // エンコードされたデータがある場合
    webrtc::EncodedImage encoded_image;
    encoded_image.SetEncodedData(webrtc::EncodedImageBuffer::Create(
        static_cast<const uint8_t*>(out_packet_.data.packet.p_data),
        out_packet_.data.packet.data_len));
    encoded_image._encodedWidth = width_;
    encoded_image._encodedHeight = height_;
    encoded_image.SetRtpTimestamp(
        static_cast<uint32_t>(out_packet_.data.packet.pts));
    encoded_image.capture_time_ms_ = frame.render_time_ms();
    encoded_image._frameType =
        (out_packet_.data.packet.frame_type == 0)  // 0=Iフレーム
            ? webrtc::VideoFrameType::kVideoFrameKey
            : webrtc::VideoFrameType::kVideoFrameDelta;
    encoded_image.content_type_ = webrtc::VideoContentType::UNSPECIFIED;

    // コーデック固有の情報を設定
    webrtc::CodecSpecificInfo codec_info;
    codec_info.codecType = codec_type_;

    if (codec_type_ == webrtc::VideoCodecType::kVideoCodecH264) {
      codec_info.codecSpecific.H264.packetization_mode =
          webrtc::H264PacketizationMode::NonInterleaved;
    }

    // コールバックを呼び出し
    webrtc::EncodedImageCallback::Result result =
        callback_->OnEncodedImage(encoded_image, &codec_info);
    if (result.error != webrtc::EncodedImageCallback::Result::OK) {
      RTC_LOG(LS_ERROR) << "Callback failed: " << result.error;
      return WEBRTC_VIDEO_CODEC_ERROR;
    }
  } else if (ret != NI_RETCODE_FAILURE) {
    // エラーの場合
    RTC_LOG(LS_ERROR) << "Failed to read from encoder: " << ret;
    return WEBRTC_VIDEO_CODEC_ERROR;
  }

  return WEBRTC_VIDEO_CODEC_OK;
}

void NetintVideoEncoderImpl::SetRates(const RateControlParameters& parameters) {
  std::lock_guard<std::mutex> lock(mutex_);

  if (!initialized_) {
    return;
  }

  uint32_t new_bitrate_bps = parameters.bitrate.get_sum_bps();
  bitrate_adjuster_.SetTargetBitrateBps(new_bitrate_bps);

  // Netint エンコーダーのビットレートを更新
  // ビットレートの動的変更はni_reconfig_bitrateを使用
  ni_reconfig_bitrate(&encoder_ctx_, new_bitrate_bps / 1000);  // bps to kbps
  // 動的ビットレート変更は新 API でサポートされていない
}

webrtc::VideoEncoder::EncoderInfo NetintVideoEncoderImpl::GetEncoderInfo()
    const {
  webrtc::VideoEncoder::EncoderInfo info;
  info.implementation_name = "NetintLibxcoder";
  info.supports_native_handle = false;
  info.is_hardware_accelerated = true;
  // has_internal_source は削除された
  info.supports_simulcast = false;

  // スケーリング設定
  info.scaling_settings =
      webrtc::VideoEncoder::ScalingSettings(10, 51);  // H264 QP の一般的な範囲

  // FPSの割り当て
  info.fps_allocation[0].resize(webrtc::kMaxTemporalStreams);
  for (size_t i = 0; i < webrtc::kMaxTemporalStreams; ++i) {
    info.fps_allocation[0][i] = 255;
  }

  return info;
}

ni_retcode_t NetintVideoEncoderImpl::ConvertCodecType(
    webrtc::VideoCodecType type,
    ni_codec_t& ni_codec) {
  switch (type) {
    case webrtc::VideoCodecType::kVideoCodecH264:
      ni_codec = (ni_codec_t)NI_CODEC_FORMAT_H264;
      return NI_RETCODE_SUCCESS;
    case webrtc::VideoCodecType::kVideoCodecH265:
      ni_codec = (ni_codec_t)NI_CODEC_FORMAT_H265;
      return NI_RETCODE_SUCCESS;
    case webrtc::VideoCodecType::kVideoCodecAV1:
      ni_codec = (ni_codec_t)NI_CODEC_FORMAT_AV1;
      return NI_RETCODE_SUCCESS;
    default:
      return NI_RETCODE_INVALID_PARAM;
  }
}

bool NetintVideoEncoderImpl::AllocateEncoderContext() {
  // エンコーダーセッションコンテキストを初期化
  ni_retcode_t ret = ni_device_session_context_init(&encoder_ctx_);
  if (ret != NI_RETCODE_SUCCESS) {
    RTC_LOG(LS_ERROR) << "Failed to init encoder context: " << ret;
    return false;
  }

  // コーデックタイプを設定
  ni_codec_t ni_codec;
  if (ConvertCodecType(codec_type_, ni_codec) != NI_RETCODE_SUCCESS) {
    RTC_LOG(LS_ERROR) << "Unsupported codec type";
    ni_device_session_context_clear(&encoder_ctx_);
    return false;
  }

  // デバイスを割り当て - 新しいAPIシグネチャ
  uint64_t load = 0;
  ni_device_context_t* dev_ctx = ni_rsrc_allocate_auto(
      NI_DEVICE_TYPE_ENCODER,
      EN_ALLOC_LEAST_LOAD,  // 最小負荷のデバイスを選択
      ni_codec,
      width_ > 0 ? width_ : 1920,        // デフォルト幅
      height_ > 0 ? height_ : 1080,      // デフォルト高さ
      framerate_ > 0 ? framerate_ : 30,  // デフォルトフレームレート
      &load);

  if (dev_ctx == nullptr) {
    RTC_LOG(LS_ERROR) << "Failed to allocate encoder device";
    ni_device_session_context_clear(&encoder_ctx_);
    return false;
  }

  // デバイスコンテキストのパラメータをセッションコンテキストにコピー
  encoder_ctx_.hw_id = dev_ctx->p_device_info->hw_id;
  encoder_ctx_.codec_format = ni_codec;
  encoder_ctx_.keep_alive_timeout = 10;

  // セッションをオープン
  ret = ni_device_session_open(&encoder_ctx_, NI_DEVICE_TYPE_ENCODER);
  if (ret != NI_RETCODE_SUCCESS) {
    RTC_LOG(LS_ERROR) << "Failed to open encoder session: " << ret;
    ni_rsrc_free_device_context(dev_ctx);
    ni_device_session_context_clear(&encoder_ctx_);
    return false;
  }

  // デバイスコンテキストを解放（セッションオープン後は不要）
  ni_rsrc_free_device_context(dev_ctx);

  return true;
}

void NetintVideoEncoderImpl::ReleaseEncoderContext() {
  // セッションをフラッシュ
  ni_device_session_flush(&encoder_ctx_, NI_DEVICE_TYPE_ENCODER);

  // セッションをクローズ
  ni_device_session_close(&encoder_ctx_, 1, NI_DEVICE_TYPE_ENCODER);

  // セッションコンテキストに関連するリソースはセッションクローズで解放される

  // コンテキストをクリア
  ni_device_session_context_clear(&encoder_ctx_);

  // I/Oバッファをクリア
  ni_frame_buffer_free(&in_frame_.data.frame);
  ni_packet_buffer_free(&out_packet_.data.packet);
}

int32_t NetintVideoEncoderImpl::ConfigureEncoder(
    const webrtc::VideoCodec* codec_settings) {
  // コーデックタイプを変換
  ni_codec_t ni_codec;
  if (ConvertCodecType(codec_type_, ni_codec) != NI_RETCODE_SUCCESS) {
    RTC_LOG(LS_ERROR) << "Unsupported codec type";
    return WEBRTC_VIDEO_CODEC_ERROR;
  }

  // ni_xcoder_params_t を使用してエンコーダーパラメータを設定
  ni_xcoder_params_t params = {};
  ni_encoder_init_default_params(&params, framerate_, 1, target_bitrate_bps_,
                                 width_, height_, (ni_codec_format_t)ni_codec);

  // パラメータをセッションコンテキストに設定
  encoder_ctx_.p_session_config = &params;
  encoder_ctx_.active_video_width = width_;
  encoder_ctx_.active_video_height = height_;

  // エンコーダーの初期化パラメータはセッションオープン時に設定済み

  // I/Oバッファを割り当て
  // ni_frame_buffer_alloc(p_frame, video_width, video_height, alignment, metadata_flag, factor, hw_frame_count, is_planar)
  ni_retcode_t ret = ni_frame_buffer_alloc(&in_frame_.data.frame, width_,
                                           height_, 0, 0, 1, 0, 1);
  if (ret != NI_RETCODE_SUCCESS) {
    RTC_LOG(LS_ERROR) << "Failed to allocate input frame buffer: " << ret;
    return WEBRTC_VIDEO_CODEC_ERROR;
  }

  ret = ni_packet_buffer_alloc(&out_packet_.data.packet,
                               width_ * height_ * 3 / 2);
  if (ret != NI_RETCODE_SUCCESS) {
    RTC_LOG(LS_ERROR) << "Failed to allocate output packet buffer: " << ret;
    ni_frame_buffer_free(&in_frame_.data.frame);
    return WEBRTC_VIDEO_CODEC_ERROR;
  }

  return WEBRTC_VIDEO_CODEC_OK;
}

// static
bool NetintVideoEncoder::IsSupported(std::shared_ptr<NetintContext> context,
                                     webrtc::VideoCodecType codec) {
  if (!context) {
    return false;
  }

  // デバイスプールを取得
  ni_device_pool_t* device_pool = ni_rsrc_get_device_pool();
  if (device_pool == nullptr || device_pool->p_device_queue == nullptr) {
    return false;
  }

  bool supported = false;

  // エンコーダーデバイスをチェック
  for (int i = 0; i < NI_MAX_DEVICE_CNT; i++) {
    int guid = device_pool->p_device_queue->xcoders[NI_DEVICE_TYPE_ENCODER][i];
    if (guid >= 0) {
      // デバイス情報を取得
      ni_device_info_t* device_info =
          ni_rsrc_get_device_info(NI_DEVICE_TYPE_ENCODER, guid);
      if (device_info != nullptr) {
        // 各コーデックのサポートを確認
        for (int codec_idx = 0; codec_idx < EN_CODEC_MAX; codec_idx++) {
          int codec_format = device_info->dev_cap[codec_idx].supports_codec;
          switch (codec) {
            case webrtc::VideoCodecType::kVideoCodecH264:
              if (codec_format == (int)NI_CODEC_FORMAT_H264) {
                supported = true;
              }
              break;
            case webrtc::VideoCodecType::kVideoCodecH265:
              if (codec_format == (int)NI_CODEC_FORMAT_H265) {
                supported = true;
              }
              break;
            case webrtc::VideoCodecType::kVideoCodecAV1:
              if (codec_format == (int)NI_CODEC_FORMAT_AV1) {
                supported = true;
              }
              break;
            default:
              break;
          }
          if (supported) {
            break;
          }
        }
      }
      if (supported) {
        break;  // 最初のエンコーダーデバイスのみチェック
      }
    }
  }

  ni_rsrc_free_device_pool(device_pool);
  return supported;
}

// static
std::unique_ptr<NetintVideoEncoder> NetintVideoEncoder::Create(
    std::shared_ptr<NetintContext> context,
    webrtc::VideoCodecType codec) {
  if (!IsSupported(context, codec)) {
    RTC_LOG(LS_ERROR) << "Codec not supported by Netint: "
                      << static_cast<int>(codec);
    return nullptr;
  }

  return std::make_unique<NetintVideoEncoderImpl>(context, codec);
}

}  // namespace sora