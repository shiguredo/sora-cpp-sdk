#ifndef SORA_VERSION_H_
#define SORA_VERSION_H_

#include <string>

namespace sora {

class Version {
 public:
  static std::string GetClientName();
  static std::string GetLibwebrtcName();
  static std::string GetEnvironmentName();
};

}  // namespace sora

#endif
