/*
 *  Copyright (c) 2015 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 *
 */

#ifndef SORA_HWENC_JETSON_JETSON_VIDEO_ENCODER_H_
#define SORA_HWENC_JETSON_JETSON_VIDEO_ENCODER_H_

#include <chrono>
#include <memory>
#include <queue>

// Linux
#include <linux/videodev2.h>

// WebRTC
#include <api/video_codecs/video_encoder.h>
#include <common_video/include/bitrate_adjuster.h>
#include <media/base/codec.h>
#include <modules/video_coding/codecs/h264/include/h264.h>
#include <modules/video_coding/codecs/vp9/include/vp9_globals.h>
#include <modules/video_coding/svc/scalable_video_controller.h>
#include <rtc_base/synchronization/mutex.h>

#include "jetson_jpeg_decoder.h"

#define CONVERTER_CAPTURE_NUM 2

class NvBuffer;
class NvV4l2Element;
class NvVideoEncoder;
struct v4l2_ctrl_videoenc_outputbuf_metadata_;

namespace sora {

class JetsonVideoEncoder : public webrtc::VideoEncoder {
 public:
  explicit JetsonVideoEncoder(const cricket::Codec& codec);
  ~JetsonVideoEncoder() override;

  static bool IsSupported(webrtc::VideoCodecType codec);

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
  struct FrameParams {
    FrameParams(int32_t w,
                int32_t h,
                int64_t rtms,
                int64_t ntpms,
                int64_t tsus,
                int64_t rtpts,
                webrtc::VideoRotation r,
                absl::optional<webrtc::ColorSpace> c,
                std::shared_ptr<JetsonJpegDecoder> d)
        : width(w),
          height(h),
          render_time_ms(rtms),
          ntp_time_ms(ntpms),
          timestamp_us(tsus),
          timestamp_rtp(rtpts),
          rotation(r),
          color_space(c),
          decoder_(d) {}

    int32_t width;
    int32_t height;
    int64_t render_time_ms;
    int64_t ntp_time_ms;
    int64_t timestamp_us;
    int64_t timestamp_rtp;
    webrtc::VideoRotation rotation;
    absl::optional<webrtc::ColorSpace> color_space;
    std::shared_ptr<JetsonJpegDecoder> decoder_;
  };

  int32_t JetsonConfigure();
  void JetsonRelease();
  void SendEOS();
  static bool EncodeFinishedCallbackFunction(struct v4l2_buffer* v4l2_buf,
                                             NvBuffer* buffer,
                                             NvBuffer* shared_buffer,
                                             void* data);
  bool EncodeFinishedCallback(struct v4l2_buffer* v4l2_buf,
                              NvBuffer* buffer,
                              NvBuffer* shared_buffer);
  void SetFramerate(uint32_t framerate);
  void SetBitrateBps(uint32_t bitrate_bps);
  int32_t SendFrame(unsigned char* buffer,
                    size_t size,
                    std::unique_ptr<FrameParams> params,
                    v4l2_ctrl_videoenc_outputbuf_metadata_* enc_metadata);

  webrtc::VideoCodec codec_;
  webrtc::EncodedImageCallback* callback_;
  NvVideoEncoder* encoder_;
  std::unique_ptr<webrtc::BitrateAdjuster> bitrate_adjuster_;
  uint32_t framerate_;
  int32_t configured_framerate_;
  uint32_t target_bitrate_bps_;
  uint32_t configured_bitrate_bps_;
  int key_frame_interval_;
  uint32_t decode_pixfmt_;
  uint32_t raw_width_;
  uint32_t raw_height_;
  int32_t width_;
  int32_t height_;
  bool use_native_;
  bool use_dmabuff_;
  int dmabuff_fd_[CONVERTER_CAPTURE_NUM];

  webrtc::GofInfoVP9 gof_;
  size_t gof_idx_;

  std::unique_ptr<webrtc::ScalableVideoController> svc_controller_;

  webrtc::Mutex frame_params_lock_;
  std::queue<std::unique_ptr<FrameParams>> frame_params_;
  std::mutex enc0_buffer_mtx_;
  std::condition_variable enc0_buffer_cond_;
  std::queue<NvBuffer*>* enc0_buffer_queue_;
  int output_plane_fd_[32];
  webrtc::EncodedImage encoded_image_;
  webrtc::ScalabilityMode scalability_mode_;
  std::vector<uint8_t> obu_seq_header_;
};

}  // namespace sora

#endif
