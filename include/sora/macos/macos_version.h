#ifndef SORA_MACOS_MACOS_VERSION_H_
#define SORA_MACOS_MACOS_VERSION_H_

#include <string>

namespace sora {

class MacosVersion {
 public:
  static std::string GetOSName();
  static std::string GetOSVersion();
};

}  // namespace sora

#endif  // MACOS_VERSION_H_
