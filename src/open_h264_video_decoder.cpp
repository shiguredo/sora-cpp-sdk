#include "sora/open_h264_video_decoder.h"

#include <memory>

#if defined(_WIN32)
// Windows
#include <windows.h>
#else
// Linux
#include <dlfcn.h>
#endif

// WebRTC
#include <api/video/i420_buffer.h>
#include <common_video/h264/h264_bitstream_parser.h>
#include <modules/video_coding/codecs/h264/include/h264.h>
#include <modules/video_coding/include/video_error_codes.h>
#include <rtc_base/logging.h>
#include <third_party/libyuv/include/libyuv.h>

// OpenH264
#include <wels/codec_api.h>
#include <wels/codec_app_def.h>
#include <wels/codec_def.h>
#include <wels/codec_ver.h>

class ISVCDecoder;

namespace webrtc {

class OpenH264VideoDecoder : public H264Decoder {
 public:
  static std::unique_ptr<VideoDecoder> Create(std::string openh264) {
    return std::unique_ptr<VideoDecoder>(
        new OpenH264VideoDecoder(std::move(openh264)));
  }

  OpenH264VideoDecoder(std::string openh264);
  ~OpenH264VideoDecoder() override;

  bool Configure(const Settings& settings) override;
  int32_t Release() override;

  int32_t RegisterDecodeCompleteCallback(
      DecodedImageCallback* callback) override;

  int32_t Decode(const EncodedImage& input_image,
                 bool missing_frames,
                 int64_t render_time_ms = -1) override;

  const char* ImplementationName() const override;

 private:
  DecodedImageCallback* callback_ = nullptr;
  ISVCDecoder* decoder_ = nullptr;
  webrtc::H264BitstreamParser h264_bitstream_parser_;

  std::string openh264_;
#if defined(_WIN32)
  HMODULE openh264_handle_ = nullptr;
#else
  void* openh264_handle_ = nullptr;
#endif
  using CreateDecoderFunc = int (*)(ISVCDecoder**);
  using DestroyDecoderFunc = void (*)(ISVCDecoder*);
  CreateDecoderFunc create_decoder_ = nullptr;
  DestroyDecoderFunc destroy_decoder_ = nullptr;
};

OpenH264VideoDecoder::OpenH264VideoDecoder(std::string openh264)
    : openh264_(std::move(openh264)) {}
OpenH264VideoDecoder::~OpenH264VideoDecoder() {
  Release();
}

bool OpenH264VideoDecoder::Configure(const Settings& settings) {
  Release();

#if defined(_WIN32)
  HMODULE handle = LoadLibraryA(openh264_.c_str());
#else
  void* handle = ::dlopen(openh264_.c_str(), RTLD_LAZY);
#endif
  if (handle == nullptr) {
    RTC_LOG(LS_ERROR) << "Failed to dlopen";
    return false;
  }
  openh264_handle_ = handle;
#if defined(_WIN32)
  create_decoder_ =
      (CreateDecoderFunc)::GetProcAddress(handle, "WelsCreateDecoder");
  if (create_decoder_ == nullptr) {
    RTC_LOG(LS_ERROR) << "Failed to GetProcAddress(WelsCreateDecoder)";
    Release();
    return false;
  }
  destroy_decoder_ =
      (DestroyDecoderFunc)::GetProcAddress(handle, "WelsDestroyDecoder");
  if (destroy_decoder_ == nullptr) {
    RTC_LOG(LS_ERROR) << "Failed to GetProcAddress(WelsDestroyDecoder)";
    Release();
    return false;
  }
#else
  create_decoder_ = (CreateDecoderFunc)::dlsym(handle, "WelsCreateDecoder");
  if (create_decoder_ == nullptr) {
    RTC_LOG(LS_ERROR) << "Failed to dlsym(WelsCreateDecoder)";
    Release();
    return false;
  }
  destroy_decoder_ = (DestroyDecoderFunc)::dlsym(handle, "WelsDestroyDecoder");
  if (destroy_decoder_ == nullptr) {
    RTC_LOG(LS_ERROR) << "Failed to dlsym(WelsDestroyDecoder)";
    Release();
    return false;
  }
#endif

  ISVCDecoder* decoder = nullptr;
  int r = create_decoder_(&decoder);
  if (r != 0) {
    RTC_LOG(LS_ERROR) << "Failed to WelsCreateDecoder: r=" << r;
    Release();
    return false;
  }

  SDecodingParam param = {};
  r = decoder->Initialize(&param);
  if (r != 0) {
    RTC_LOG(LS_ERROR) << "Failed to ISVCDecoder::Initialize: r=" << r;
    Release();
    return false;
  }
  decoder_ = decoder;

  return true;
}
int32_t OpenH264VideoDecoder::Release() {
  if (decoder_ != nullptr) {
    decoder_->Uninitialize();
    destroy_decoder_(decoder_);
    decoder_ = nullptr;
  }

  if (openh264_handle_ != nullptr) {
#if defined(_WIN32)
    FreeLibrary(openh264_handle_);
#else
    ::dlclose(openh264_handle_);
#endif
    openh264_handle_ = nullptr;
  }

  return WEBRTC_VIDEO_CODEC_OK;
}

int32_t OpenH264VideoDecoder::RegisterDecodeCompleteCallback(
    DecodedImageCallback* callback) {
  callback_ = callback;
  return WEBRTC_VIDEO_CODEC_OK;
}

int32_t OpenH264VideoDecoder::Decode(const EncodedImage& input_image,
                                     bool missing_frames,
                                     int64_t render_time_ms) {
  if (decoder_ == nullptr) {
    return WEBRTC_VIDEO_CODEC_UNINITIALIZED;
  }

  h264_bitstream_parser_.ParseBitstream(input_image);
  std::optional<int> qp = h264_bitstream_parser_.GetLastSliceQp();

  std::array<std::uint8_t*, 3> yuv;
  SBufferInfo info = {};
  int r = decoder_->DecodeFrameNoDelay(input_image.data(), input_image.size(),
                                       yuv.data(), &info);
  if (r != 0) {
    RTC_LOG(LS_ERROR) << "Failed to ISVCDecoder::DecodeFrameNoDelay: r=" << r;
    return WEBRTC_VIDEO_CODEC_ERROR;
  }

  if (info.iBufferStatus == 0) {
    return WEBRTC_VIDEO_CODEC_OK;
  }

  int width_y = info.UsrData.sSystemBuffer.iWidth;
  int height_y = info.UsrData.sSystemBuffer.iHeight;
  int width_uv = (width_y + 1) / 2;
  int height_uv = (height_y + 1) / 2;
  int stride_y = info.UsrData.sSystemBuffer.iStride[0];
  int stride_uv = info.UsrData.sSystemBuffer.iStride[1];
  rtc::scoped_refptr<webrtc::I420Buffer> i420_buffer(
      webrtc::I420Buffer::Create(width_y, height_y));
  libyuv::I420Copy(yuv[0], stride_y, yuv[1], stride_uv, yuv[2], stride_uv,
                   i420_buffer->MutableDataY(), i420_buffer->StrideY(),
                   i420_buffer->MutableDataU(), i420_buffer->StrideU(),
                   i420_buffer->MutableDataV(), i420_buffer->StrideV(), width_y,
                   height_y);

  webrtc::VideoFrame video_frame =
      webrtc::VideoFrame::Builder()
          .set_video_frame_buffer(i420_buffer)
          .set_timestamp_rtp(input_image.RtpTimestamp())
          .build();
  if (input_image.ColorSpace() != nullptr) {
    video_frame.set_color_space(*input_image.ColorSpace());
  }

  callback_->Decoded(video_frame, std::nullopt, qp);

  return WEBRTC_VIDEO_CODEC_OK;
}

const char* OpenH264VideoDecoder::ImplementationName() const {
  return "OpenH264";
}

}  // namespace webrtc

namespace sora {

std::unique_ptr<webrtc::VideoDecoder> CreateOpenH264VideoDecoder(
    const webrtc::SdpVideoFormat& format,
    std::string openh264) {
  return webrtc::OpenH264VideoDecoder::Create(std::move(openh264));
}

}  // namespace sora
