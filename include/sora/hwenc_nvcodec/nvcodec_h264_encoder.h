#ifndef SORA_HWENC_NVCODEC_NVCODEC_H264_ENCODER_H_
#define SORA_HWENC_NVCODEC_NVCODEC_H264_ENCODER_H_

#ifdef _WIN32
#include <d3d11.h>
#include <wrl.h>
#endif

#include <chrono>
#include <memory>
#include <mutex>
#include <queue>

// WebRTC
#include <api/video_codecs/video_encoder.h>
#include <common_video/h264/h264_bitstream_parser.h>
#include <common_video/include/bitrate_adjuster.h>
#include <modules/video_coding/codecs/h264/include/h264.h>

// NvCodec
#ifdef _WIN32
#include <NvEncoder/NvEncoderD3D11.h>
#endif
#ifdef __linux__
#include "nvcodec_h264_encoder_cuda.h"
#endif

#include "sora/cuda_context.h"

namespace sora {

class NvCodecH264Encoder : public webrtc::VideoEncoder {
 public:
  explicit NvCodecH264Encoder(const cricket::VideoCodec& codec,
                              std::shared_ptr<CudaContext> cuda_context);
  ~NvCodecH264Encoder() override;

  static bool IsSupported(std::shared_ptr<CudaContext> cuda_context);

  int32_t InitEncode(const webrtc::VideoCodec* codec_settings,
                     int32_t number_of_cores,
                     size_t max_payload_size) override;
  int32_t RegisterEncodeCompleteCallback(
      webrtc::EncodedImageCallback* callback) override;
  int32_t Release() override;
  int32_t Encode(
      const webrtc::VideoFrame& frame,
      const std::vector<webrtc::VideoFrameType>* frame_types) override;
  void SetRates(
      const webrtc::VideoEncoder::RateControlParameters& parameters) override;
  webrtc::VideoEncoder::EncoderInfo GetEncoderInfo() const override;

 private:
  std::mutex mutex_;
  webrtc::EncodedImageCallback* callback_ = nullptr;
  webrtc::BitrateAdjuster bitrate_adjuster_;
  uint32_t target_bitrate_bps_ = 0;
  uint32_t max_bitrate_bps_ = 0;

  int32_t InitNvEnc();
  int32_t ReleaseNvEnc();
  webrtc::H264BitstreamParser h264_bitstream_parser_;

  static std::unique_ptr<NvEncoder> CreateEncoder(
      int width,
      int height,
      int framerate,
      int target_bitrate_bps,
      int max_bitrate_bps
#ifdef _WIN32
      ,
      ID3D11Device* id3d11_device,
      ID3D11Texture2D** out_id3d11_texture
#endif
#ifdef __linux__
      , NvCodecH264EncoderCuda* cuda
      , bool is_nv12
#endif
  );

  std::shared_ptr<CudaContext> cuda_context_;
  std::unique_ptr<NvEncoder> nv_encoder_;
#ifdef _WIN32
  Microsoft::WRL::ComPtr<ID3D11Device> id3d11_device_;
  Microsoft::WRL::ComPtr<ID3D11DeviceContext> id3d11_context_;
  Microsoft::WRL::ComPtr<ID3D11Texture2D> id3d11_texture_;
#endif
#ifdef __linux__
  std::unique_ptr<NvCodecH264EncoderCuda> cuda_;
  bool is_nv12_ = false;
#endif
  bool reconfigure_needed_ = false;
  uint32_t width_ = 0;
  uint32_t height_ = 0;
  uint32_t framerate_ = 0;
  webrtc::VideoCodecMode mode_ = webrtc::VideoCodecMode::kRealtimeVideo;
  NV_ENC_INITIALIZE_PARAMS initialize_params_;
  std::vector<std::vector<uint8_t>> v_packet_;
  webrtc::EncodedImage encoded_image_;
};

}  // namespace sora

#endif
