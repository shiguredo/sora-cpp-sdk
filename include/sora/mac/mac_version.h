#ifndef SORA_MAC_MAC_VERSION_H_
#define SORA_MAC_MAC_VERSION_H_

#include <string>

namespace sora {

class MacVersion {
 public:
  static std::string GetOSName();
  static std::string GetOSVersion();
};

}  // namespace sora

#endif
