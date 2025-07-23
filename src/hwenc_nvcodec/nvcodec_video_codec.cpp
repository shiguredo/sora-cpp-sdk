#include "sora/hwenc_nvcodec/nvcodec_video_codec.h"

#include <memory>
#include <string>

// WebRTC
#include <api/video/video_codec_type.h>

#include "nvcodec_video_codec_cuda.h"
#include "sora/cuda_context.h"
#include "sora/hwenc_nvcodec/nvcodec_video_decoder.h"
#include "sora/hwenc_nvcodec/nvcodec_video_encoder.h"
#include "sora/sora_video_codec.h"

#if defined(_WIN32)
#include <d3d11.h>
#include <wrl.h>
#endif

namespace sora {

namespace {

sora::VideoCodecCapability::Parameters GetParameters(
    std::shared_ptr<CudaContext> context) {
  sora::VideoCodecCapability::Parameters p;
#if defined(_WIN32)
  Microsoft::WRL::ComPtr<IDXGIFactory1> idxgi_factory;
  if (FAILED(CreateDXGIFactory1(__uuidof(IDXGIFactory1),
                                (void**)idxgi_factory.GetAddressOf()))) {
    return p;
  }
  Microsoft::WRL::ComPtr<IDXGIAdapter> idxgi_adapter;
  if (FAILED(idxgi_factory->EnumAdapters(0, idxgi_adapter.GetAddressOf()))) {
    return p;
  }
  // 以下デバイス名を取得するだけの処理
  DXGI_ADAPTER_DESC adapter_desc;
  idxgi_adapter->GetDesc(&adapter_desc);
  char szDesc[80];
  size_t result = 0;
  wcstombs_s(&result, szDesc, adapter_desc.Description, sizeof(szDesc));
  p.nvcodec_gpu_device_name = std::string(szDesc);
#endif
#if defined(__linux__)
  if (context == nullptr) {
    return p;
  }

  char name[80] = {};
  GetNvCodecGpuDeviceName(context, name, sizeof(name));
  if (name[0] != '\0') {
    p.nvcodec_gpu_device_name = std::string(name);
  }
#endif

  return p;
}

}  // namespace

VideoCodecCapability::Engine GetNvCodecVideoCodecCapability(
    std::shared_ptr<CudaContext> context) {
  VideoCodecCapability::Engine engine(
      VideoCodecImplementation::kNvidiaVideoCodecSdk);
  engine.parameters = GetParameters(context);

  auto add = [&engine, &context](CudaVideoCodec type,
                                 webrtc::VideoCodecType webrtc_type) {
    engine.codecs.emplace_back(webrtc_type,
                               NvCodecVideoEncoder::IsSupported(context, type),
                               NvCodecVideoDecoder::IsSupported(context, type));
  };
  add(CudaVideoCodec::VP8, webrtc::kVideoCodecVP8);
  add(CudaVideoCodec::VP9, webrtc::kVideoCodecVP9);
  add(CudaVideoCodec::H264, webrtc::kVideoCodecH264);
  add(CudaVideoCodec::H265, webrtc::kVideoCodecH265);
  add(CudaVideoCodec::AV1, webrtc::kVideoCodecAV1);
  return engine;
}

}  // namespace sora
