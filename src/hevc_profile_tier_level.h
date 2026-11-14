#ifndef SORA_HEVC_PROFILE_TIER_LEVEL_H_
#define SORA_HEVC_PROFILE_TIER_LEVEL_H_

#include <optional>
#include <string>
#include <vector>

// WebRTC
#include <api/video_codecs/sdp_video_format.h>

// Intel VPL
#ifdef SORA_CPP_SDK_USE_INTEL_VPL
#include <vpl/mfxdefs.h>
#include <vpl/mfxstructures.h>
#endif

// AMD AMF
#ifdef SORA_CPP_SDK_USE_AMD_AMF
#include <AMF/components/VideoEncoderHEVC.h>
#endif

namespace sora {

// HEVC プロファイル、ティア、レベルを管理する構造体
struct HevcProfileTierLevel {
  int profile_id;  // 1: Main, 2: Main 10
  int tier_flag;   // 0: Main Tier, 1: High Tier
  int level_id;    // 93: Level 3.1, 120: Level 4.0, etc.
};

// Intel VPL のレベル値を SDP level-id に変換
// VPL: 31 (MFX_LEVEL_HEVC_31) -> SDP: 93
inline int ConvertVplLevelToSdpLevelId(int vpl_level) {
  // VPL のレベル値を変換
  // 31 -> 3.1 -> 93
  // 40 -> 4.0 -> 120
  if (vpl_level == 10) {  // MFX_LEVEL_HEVC_1
    return 30;
  } else if (vpl_level == 20) {  // MFX_LEVEL_HEVC_2
    return 60;
  } else if (vpl_level == 21) {  // MFX_LEVEL_HEVC_21
    return 63;
  } else if (vpl_level == 30) {  // MFX_LEVEL_HEVC_3
    return 90;
  } else if (vpl_level == 31) {  // MFX_LEVEL_HEVC_31
    return 93;
  } else if (vpl_level == 40) {  // MFX_LEVEL_HEVC_4
    return 120;
  } else if (vpl_level == 41) {  // MFX_LEVEL_HEVC_41
    return 123;
  } else if (vpl_level == 50) {  // MFX_LEVEL_HEVC_5
    return 150;
  } else if (vpl_level == 51) {  // MFX_LEVEL_HEVC_51
    return 153;
  } else if (vpl_level == 52) {  // MFX_LEVEL_HEVC_52
    return 156;
  } else if (vpl_level == 60) {  // MFX_LEVEL_HEVC_6
    return 180;
  } else if (vpl_level == 61) {  // MFX_LEVEL_HEVC_61
    return 183;
  } else if (vpl_level == 62) {  // MFX_LEVEL_HEVC_62
    return 186;
  } else {
    return 93;  // デフォルトは Level 3.1
  }
}

// AMD AMF のレベル値は既に SDP level-id と同じ
// AMF: 93 (AMF_LEVEL_3_1) -> SDP: 93
inline int ConvertAmfLevelToSdpLevelId(int amf_level) {
  return amf_level;  // AMF のレベル値はそのまま使える
}

// ハードウェアエンコーダーのタイプ
enum class HevcHardwareType {
  kIntelVpl,
  kAmdAmf,
  kNvidiaVideo,
  kAppleVideoToolbox,
  kUnknown  // ハードウェアタイプが不明な場合
};

// ハードウェアエンコーダーに応じた HEVC フォーマットを生成
std::vector<webrtc::SdpVideoFormat> GetHevcFormatsForHardware(
    HevcHardwareType hw_type,
    std::optional<int> max_level = std::nullopt);

}  // namespace sora

#endif  // SORA_HEVC_PROFILE_TIER_LEVEL_H_