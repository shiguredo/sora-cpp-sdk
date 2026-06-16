#ifndef SORA_AUDIO_DEVICE_HELPER_H_
#define SORA_AUDIO_DEVICE_HELPER_H_

#include <string>
#include <tuple>
#include <vector>

namespace sora {

// デバイス名または GUID と一致するデバイスのインデックスを探す
int FindAudioDeviceIndex(
    const std::string& device_name,
    const std::vector<std::tuple<std::string, std::string> >& devices);

}  // namespace sora

#endif
