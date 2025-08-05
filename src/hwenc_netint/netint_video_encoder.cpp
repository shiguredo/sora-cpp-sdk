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
  rtc::scoped_refptr<webrtc::I420BufferInterface> i420_buffer =
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
  in_frame_.data.frame.pts = frame.timestamp();
  in_frame_.data.frame.dts = frame.timestamp();
  in_frame_.data.frame.force_key_frame = force_key_frame ? 1 : 0;

  // エンコード実行
  ni_retcode_t ret = ni_device_session_write(&encoder_ctx_, &in_frame_, NI_DEVICE_TYPE_ENCODER);
  if (ret != NI_RETCODE_SUCCESS) {
    RTC_LOG(LS_ERROR) << "Failed to write frame to encoder: " << ret;
    return WEBRTC_VIDEO_CODEC_ERROR;
  }

  // エンコード結果を読み取り
  ret = ni_device_session_read(&encoder_ctx_, &out_packet_, NI_DEVICE_TYPE_ENCODER);
  if (ret == NI_RETCODE_SUCCESS) {
    // エンコードされたデータがある場合
    webrtc::EncodedImage encoded_image;
    encoded_image.SetEncodedData(webrtc::EncodedImageBuffer::Create(
        out_packet_.data.packet.p_data, out_packet_.data.packet.data_len));
    encoded_image._encodedWidth = width_;
    encoded_image._encodedHeight = height_;
    encoded_image.SetTimestamp(out_packet_.data.packet.pts);
    encoded_image.capture_time_ms_ = frame.render_time_ms();
    encoded_image._frameType = (out_packet_.data.packet.key_frame)
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
  encoder_ctx_.bit_rate = new_bitrate_bps / 1000;  // bps to kbps
  // 動的ビットレート変更は新 API でサポートされていない
}

webrtc::VideoEncoder::EncoderInfo NetintVideoEncoderImpl::GetEncoderInfo()
    const {
  webrtc::VideoEncoder::EncoderInfo info;
  info.implementation_name = "NetintLibxcoder";
  info.supports_native_handle = false;
  info.is_hardware_accelerated = true;
  info.has_internal_source = false;
  info.supports_simulcast = false;

  // スケーリング設定
  info.scaling_settings = webrtc::VideoEncoder::ScalingSettings(
      webrtc::kLowH264QpThreshold, webrtc::kHighH264QpThreshold);

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

bool NetintVideoEncoderImpl::AllocateEncoderContext() {
  // エンコーダーセッションコンテキストを初期化
  ni_retcode_t ret = ni_device_session_context_init(&encoder_ctx_);
  if (ret != NI_RETCODE_SUCCESS) {
    RTC_LOG(LS_ERROR) << "Failed to init encoder context: " << ret;
    return false;
  }

  // デバイスを割り当て
  ret = ni_rsrc_allocate_auto(&encoder_ctx_, NI_DEVICE_TYPE_ENCODER);
  if (ret != NI_RETCODE_SUCCESS) {
    RTC_LOG(LS_ERROR) << "Failed to allocate encoder device: " << ret;
    ni_device_session_context_clear(&encoder_ctx_);
    return false;
  }

  // セッションをオープン
  ret = ni_device_session_open(&encoder_ctx_, NI_DEVICE_TYPE_ENCODER);
  if (ret != NI_RETCODE_SUCCESS) {
    RTC_LOG(LS_ERROR) << "Failed to open encoder session: " << ret;
    ni_rsrc_free_device_context(&encoder_ctx_);
    ni_device_session_context_clear(&encoder_ctx_);
    return false;
  }

  return true;
}

void NetintVideoEncoderImpl::ReleaseEncoderContext() {
  // セッションをフラッシュ
  ni_device_session_flush(&encoder_ctx_, NI_DEVICE_TYPE_ENCODER);

  // セッションをクローズ
  ni_device_session_close(&encoder_ctx_, 1, NI_DEVICE_TYPE_ENCODER);

  // リソースを解放
  ni_rsrc_free_device_context(&encoder_ctx_);

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

  // エンコーダーパラメータを設定
  encoder_ctx_.codec_format = ni_codec;
  encoder_ctx_.bit_rate = target_bitrate_bps_ / 1000;  // bps to kbps
  encoder_ctx_.video_width = width_;
  encoder_ctx_.video_height = height_;
  encoder_ctx_.fps_number = framerate_;
  encoder_ctx_.fps_denominator = 1;
  encoder_ctx_.color_primaries = NI_COL_PRI_BT709;
  encoder_ctx_.color_trc = NI_COL_TRC_BT709;
  encoder_ctx_.color_space = NI_COL_SPC_BT709;
  encoder_ctx_.video_full_range_flag = 0;
  encoder_ctx_.profile = NI_PROFILE_AUTO;
  encoder_ctx_.level_idc = NI_LEVEL_AUTO;
  encoder_ctx_.tier = NI_TIER_DEFAULT;

  // GOP設定
  encoder_ctx_.gop_preset_index = NI_GOP_PRESET_DEFAULT;
  encoder_ctx_.intra_period = framerate_ * 2;  // 2秒ごとにキーフレーム

  // レート制御
  encoder_ctx_.rc.rc_mode = NI_RC_CBR;
  encoder_ctx_.rc.max_bitrate = max_bitrate_bps_ / 1000;  // bps to kbps

  // エンコーダーの初期化パラメータはセッションオープン時に設定済み

  // I/Oバッファを割り当て
  ret = ni_frame_buffer_alloc(&in_frame_.data.frame, width_, height_, 0);
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

  // サポートするコーデックタイプを確認
  ni_device_queue_t* device_queue = nullptr;
  ni_retcode_t ret =
      ni_rsrc_get_available_devices(&device_queue, NI_DEVICE_TYPE_ENCODER);

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
      // xcoder_devices 配列からエンコーダーを探す
      for (int i = 0; i < capability.xcoder_devices_cnt; i++) {
        ni_hw_capability_t* hw_cap = &capability.xcoder_devices[i];
        if (hw_cap->codec_type == NI_DEVICE_TYPE_ENCODER) {
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