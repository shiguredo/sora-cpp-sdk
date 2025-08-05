#include "sora/hwenc_netint/netint_video_codec.h"

#include <algorithm>

// WebRTC
#include <api/video/video_codec_type.h>

// Netint
#include <ni_device_api.h>
#include <ni_rsrc_api.h>

namespace sora {

VideoCodecCapability::Engine GetNetintVideoCodecCapability(
    std::shared_ptr<NetintContext> context) {
  if (!context) {
    return VideoCodecCapability::Engine{};
  }

  VideoCodecCapability::Engine engine;
  engine.type = VideoCodecCapability::Type::kHardware;
  engine.impl = VideoCodecImplementation::kNetintLibxcoder;

  // 利用可能なエンコーダーデバイスを確認
  ni_device_queue_t* encoder_queue = nullptr;
  ni_retcode_t ret =
      ni_rsrc_get_available_devices(&encoder_queue, NI_DEVICE_TYPE_ENCODER);

  if (ret == NI_RETCODE_SUCCESS && encoder_queue != nullptr &&
      encoder_queue->length > 0) {
    // Netint デバイスの能力を確認
    ni_device_capability_t capability = {};

    for (int i = 0; i < encoder_queue->length; i++) {
      ni_device_t* device = &encoder_queue->device[i];

      // デバイスの能力を取得
      ret = ni_rsrc_get_device_capability(device, &capability);
      if (ret == NI_RETCODE_SUCCESS) {
        // xcoder_devices 配列から各コーデックのサポートを確認
        for (int j = 0; j < capability.xcoder_devices_cnt; j++) {
          ni_hw_capability_t* hw_cap = &capability.xcoder_devices[j];
          // codec_format フィールドでコーデックタイプを判定
          if (hw_cap->codec_type == NI_DEVICE_TYPE_ENCODER) {
            // NI_CODEC_FORMAT_H264 = 0
            if (hw_cap->codec_format == NI_CODEC_FORMAT_H264) {
              if (std::find(engine.codecs.begin(), engine.codecs.end(), 
                           webrtc::VideoCodecType::kVideoCodecH264) == engine.codecs.end()) {
                engine.codecs.push_back(webrtc::VideoCodecType::kVideoCodecH264);
              }
            }
            // NI_CODEC_FORMAT_H265 = 1
            else if (hw_cap->codec_format == NI_CODEC_FORMAT_H265) {
              if (std::find(engine.codecs.begin(), engine.codecs.end(), 
                           webrtc::VideoCodecType::kVideoCodecH265) == engine.codecs.end()) {
                engine.codecs.push_back(webrtc::VideoCodecType::kVideoCodecH265);
              }
            }
            // NI_CODEC_FORMAT_AV1 = 4
            else if (hw_cap->codec_format == NI_CODEC_FORMAT_AV1) {
              if (std::find(engine.codecs.begin(), engine.codecs.end(), 
                           webrtc::VideoCodecType::kVideoCodecAV1) == engine.codecs.end()) {
                engine.codecs.push_back(webrtc::VideoCodecType::kVideoCodecAV1);
              }
            }
          }
        }

        // 最初のデバイスの能力のみ使用
        break;
      }
    }

    ni_rsrc_free_device_queue(encoder_queue);
  }

  // 利用可能なデコーダーデバイスを確認
  ni_device_queue_t* decoder_queue = nullptr;
  ret = ni_rsrc_get_available_devices(&decoder_queue, NI_DEVICE_TYPE_DECODER);

  if (ret == NI_RETCODE_SUCCESS && decoder_queue != nullptr &&
      decoder_queue->length > 0) {
    // デコーダーも利用可能
    ni_rsrc_free_device_queue(decoder_queue);
  }

  return engine;
}