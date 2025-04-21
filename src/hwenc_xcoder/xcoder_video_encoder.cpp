#include "sora/hwenc_xcoder/xcoder_video_encoder.h"

#include <chrono>
#include <mutex>

// WebRTC
#include <common_video/h264/h264_bitstream_parser.h>
#include <common_video/h265/h265_bitstream_parser.h>
#include <common_video/include/bitrate_adjuster.h>
#include <modules/video_coding/codecs/h264/include/h264.h>
#include <modules/video_coding/include/video_codec_interface.h>
#include <modules/video_coding/include/video_error_codes.h>
#include <rtc_base/logging.h>
#include <rtc_base/synchronization/mutex.h>

// libyuv
#include <libyuv.h>

extern "C" {
#include <ni_device_api.h>
}

namespace sora {

class LibxcoderVideoEncoderImpl : public LibxcoderVideoEncoder {
 public:
  LibxcoderVideoEncoderImpl(webrtc::VideoCodecType codec);
  ~LibxcoderVideoEncoderImpl() override;

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
};

LibxcoderVideoEncoderImpl::LibxcoderVideoEncoderImpl(
    webrtc::VideoCodecType codec) {}

LibxcoderVideoEncoderImpl::~LibxcoderVideoEncoderImpl() {}

////////////////////////
// LibxcoderVideoEncoder
////////////////////////

ni_codec_format_t ToNICodec(webrtc::VideoCodecType codec) {
  switch (codec) {
    case webrtc::kVideoCodecVP9:
      return NI_CODEC_FORMAT_VP9;
    case webrtc::kVideoCodecH264:
      return NI_CODEC_FORMAT_H264;
    case webrtc::kVideoCodecH265:
      return NI_CODEC_FORMAT_H265;
    case webrtc::kVideoCodecAV1:
      return NI_CODEC_FORMAT_AV1;
    default:
      return (ni_codec_format_t)-1;
  }
}
bool LibxcoderVideoEncoder::IsSupported(webrtc::VideoCodecType codec) {
  ni_session_context_t ctx;
  ni_xcoder_params_t params;
  int r;
  r = ni_device_session_context_init(&ctx);
  if (r < 0) {
    RTC_LOG(LS_ERROR) << "Failed to ni_device_session_context_init: r=" << r;
    return false;
  }
  ctx.codec_format = ToNICodec(codec);
  int guid = 0;

  r = ni_encoder_init_default_params(&params, 30, 1, 1000 * 1000, 640, 480,
                                     ToNICodec(codec));
  if (r < 0) {
    RTC_LOG(LS_ERROR) << "Failed to ni_encoder_init_default_params: r=" << r;
    return false;
  }

  // ret = encoder_open(enc_ctx, p_enc_api_param, output_total,
  //                 enc_conf_params, enc_gop_params, NULL, video_width[0],
  //                 video_height[0], 30, 1, 200000,
  //                 enc_codec_format, pix_fmt, 0, xcoderGUID, NULL,
  //                 0, (sw_pix_fmt != NI_SW_PIX_FMT_NONE) ? false : true);
  //                 // zero copy is not supported for YUV444P

  //      ret = encoder_open_session(&enc_ctx_list[i], codec_format, xcoder_guid,
  //                               &p_api_param_list[i], width, height,
  //                               pix_fmt, check_zerocopy);
  ctx.p_session_config = &params;
  ctx.session_id = NI_INVALID_SESSION_ID;

  // assign the card GUID in the encoder context and let session open
  // take care of the rest
  ctx.device_handle = NI_INVALID_DEVICE_HANDLE;
  ctx.blk_io_handle = NI_INVALID_DEVICE_HANDLE;
  ctx.hw_id = guid;

  ctx.src_endian = NI_FRAME_LITTLE_ENDIAN;
  ctx.src_bit_depth = 8;
  ctx.bit_depth_factor = 1;
  ctx.ori_width = 640;
  ctx.ori_height = 480;
  ctx.ori_bit_depth_factor = 8;
  ctx.ori_pix_fmt = NI_PIX_FMT_YUV420P;
  ctx.pixel_format = NI_PIX_FMT_YUV420P;
  params.source_width = 640;
  params.source_height = 480;
  // YUV420P
  params.cfg_enc_params.planar = NI_PIXEL_PLANAR_FORMAT_PLANAR;
  // NV12
  // params.cfg_enc_params.planar = NI_PIXEL_PLANAR_FORMAT_SEMIPLANAR;
  r = ni_device_session_open(&ctx, NI_DEVICE_TYPE_ENCODER);
  if (r != NI_RETCODE_SUCCESS) {
    RTC_LOG(LS_ERROR) << "Failed to ni_device_session_open: r=" << r;
    return false;
  }
  ni_device_session_context_clear(&ctx);

  return false;
}

std::unique_ptr<LibxcoderVideoEncoder> LibxcoderVideoEncoder::Create(
    webrtc::VideoCodecType codec) {
  return std::unique_ptr<LibxcoderVideoEncoder>(
      new LibxcoderVideoEncoderImpl(codec));
}

}  // namespace sora
