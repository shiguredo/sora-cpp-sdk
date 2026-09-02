#include "hevc_profile_tier_level.h"

// WebRTC
#include <media/base/media_constants.h>

namespace sora {

std::vector<webrtc::SdpVideoFormat> GetHevcFormatsForHardware(
    HevcHardwareType hw_type,
    std::optional<int> max_level) {
  std::vector<webrtc::SdpVideoFormat> formats;

  // デフォルトのレベル設定
  std::vector<int> supported_levels = {93};  // Level 3.1 (必須)

  // ハードウェアタイプごとの設定
  int profile_id = 1;  // Main Profile
  int tier_flag = 0;   // Main Tier

  if (hw_type == HevcHardwareType::kIntelVpl) {
    // Intel VPL は Main Profile のみサポート
    profile_id = 1;
    tier_flag = 0;
    if (max_level.has_value()) {
      supported_levels.clear();
      // VPL のレベル値を SDP level-id に変換
      int sdp_level = ConvertVplLevelToSdpLevelId(max_level.value());
      // 最高レベルのみを使用（最低でも Level 3.1）
      supported_levels.push_back(std::max(sdp_level, 93));
    }
  } else if (hw_type == HevcHardwareType::kAmdAmf) {
    // AMD AMF は Main/Main 10 Profile、Main/High Tier をサポート
    // ここでは Main Profile, Main Tier をデフォルトとする
    profile_id = 1;
    tier_flag = 0;
    if (max_level.has_value()) {
      supported_levels.clear();
      int sdp_level = ConvertAmfLevelToSdpLevelId(max_level.value());
      // 最高レベルのみを使用（最低でも Level 3.1）
      supported_levels.push_back(std::max(sdp_level, 93));
    }
  } else if (hw_type == HevcHardwareType::kAppleVideoToolbox) {
    // Apple VideoToolbox は Main Profile をサポート
    profile_id = 1;
    tier_flag = 0;
    // macOS Chrome の例に従って Level 3.1 のみ
    supported_levels = {93};
  } else if (hw_type == HevcHardwareType::kNvidiaVideo) {
    // NVIDIA は通常 Main/Main 10 Profile をサポート
    profile_id = 1;
    tier_flag = 0;
  } else {
    // ハードウェアタイプが不明な場合はデフォルトの Level 3.1 を使用
    // または空のベクターを返すことも可能
  }

  // SdpVideoFormat を生成
  for (int level : supported_levels) {
    webrtc::CodecParameterMap params = {
        {"profile-id", std::to_string(profile_id)},
        {"tier-flag", std::to_string(tier_flag)},
        {"level-id", std::to_string(level)},
        {"tx-mode", "SRST"}  // Single RTP stream on a Single media Transport
    };
    formats.push_back(webrtc::SdpVideoFormat(webrtc::kH265CodecName, params));
  }

  // Main 10 Profile もサポートする場合（AMD AMF など）
  // 注: 通常は1つのプロファイルのみを返すべきだが、
  // 必要に応じて Main 10 Profile を追加できる
  // if (hw_type == HevcHardwareType::kAmdAmf && !supported_levels.empty()) {
  //   webrtc::CodecParameterMap params_main10 = {
  //       {"profile-id", "2"},  // Main 10 Profile
  //       {"tier-flag", "0"},   // Main Tier
  //       {"level-id", std::to_string(supported_levels[0])},
  //       {"tx-mode", "SRST"}};
  //   formats.push_back(
  //       webrtc::SdpVideoFormat(webrtc::kH265CodecName, params_main10));
  // }

  return formats;
}

}  // namespace sora