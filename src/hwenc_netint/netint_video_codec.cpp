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
    return VideoCodecCapability::Engine(
        VideoCodecImplementation::kNetintLibxcoder);
  }

  VideoCodecCapability::Engine engine(
      VideoCodecImplementation::kNetintLibxcoder);

  // 利用可能なデバイスを確認
  ni_device_pool_t* device_pool = ni_rsrc_get_device_pool();

  if (device_pool != nullptr && device_pool->p_device_queue != nullptr) {
    // エンコーダーデバイスをチェック
    for (int i = 0; i < NI_MAX_DEVICE_CNT; i++) {
      int guid =
          device_pool->p_device_queue->xcoders[NI_DEVICE_TYPE_ENCODER][i];
      if (guid >= 0) {
        // デバイス情報を取得
        ni_device_info_t* device_info =
            ni_rsrc_get_device_info(NI_DEVICE_TYPE_ENCODER, guid);
        if (device_info != nullptr) {
          // 各コーデックのサポートを確認
          for (int codec_idx = 0; codec_idx < EN_CODEC_MAX; codec_idx++) {
            int codec_format = device_info->dev_cap[codec_idx].supports_codec;
            if (codec_format == (int)NI_CODEC_FORMAT_H264) {
              bool found = false;
              for (const auto& codec : engine.codecs) {
                if (codec.type == webrtc::VideoCodecType::kVideoCodecH264) {
                  found = true;
                  break;
                }
              }
              if (!found) {
                engine.codecs.push_back(VideoCodecCapability::Codec(
                    webrtc::VideoCodecType::kVideoCodecH264, true, false));
              }
            } else if (codec_format == (int)NI_CODEC_FORMAT_H265) {
              bool found = false;
              for (const auto& codec : engine.codecs) {
                if (codec.type == webrtc::VideoCodecType::kVideoCodecH265) {
                  found = true;
                  break;
                }
              }
              if (!found) {
                engine.codecs.push_back(VideoCodecCapability::Codec(
                    webrtc::VideoCodecType::kVideoCodecH265, true, false));
              }
            } else if (codec_format == (int)NI_CODEC_FORMAT_AV1) {
              bool found = false;
              for (const auto& codec : engine.codecs) {
                if (codec.type == webrtc::VideoCodecType::kVideoCodecAV1) {
                  found = true;
                  break;
                }
              }
              if (!found) {
                engine.codecs.push_back(VideoCodecCapability::Codec(
                    webrtc::VideoCodecType::kVideoCodecAV1, true, false));
              }
            }
          }
        }
        // 最初のエンコーダーデバイスのみチェック
        break;
      }
    }

    // デコーダーデバイスをチェック
    for (int i = 0; i < NI_MAX_DEVICE_CNT; i++) {
      int guid =
          device_pool->p_device_queue->xcoders[NI_DEVICE_TYPE_DECODER][i];
      if (guid >= 0) {
        // デバイス情報を取得
        ni_device_info_t* device_info =
            ni_rsrc_get_device_info(NI_DEVICE_TYPE_DECODER, guid);
        if (device_info != nullptr) {
          // 各コーデックのサポートを確認
          for (int codec_idx = 0; codec_idx < EN_CODEC_MAX; codec_idx++) {
            int codec_format = device_info->dev_cap[codec_idx].supports_codec;
            // 各コーデックのデコーダーサポートを更新
            for (auto& codec : engine.codecs) {
              if (codec.type == webrtc::VideoCodecType::kVideoCodecH264 &&
                  codec_format == (int)NI_CODEC_FORMAT_H264) {
                codec.decoder = true;
              } else if (codec.type ==
                             webrtc::VideoCodecType::kVideoCodecH265 &&
                         codec_format == (int)NI_CODEC_FORMAT_H265) {
                codec.decoder = true;
              } else if (codec.type == webrtc::VideoCodecType::kVideoCodecAV1 &&
                         codec_format == (int)NI_CODEC_FORMAT_AV1) {
                codec.decoder = true;
              }
            }
          }
        }
        // 最初のデコーダーデバイスのみチェック
        break;
      }
    }

    ni_rsrc_free_device_pool(device_pool);
  }

  // デコーダー処理は上記に統合済み

  return engine;
}

}  // namespace sora