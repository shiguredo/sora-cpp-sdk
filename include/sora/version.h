#ifndef SORA_VERSION_H_
#define SORA_VERSION_H_

#include <optional>
#include <string>

namespace sora {

typedef std::optional<std::string> http_header_value;

class Version {
 public:
  static std::string GetClientName();
  static std::string GetLibwebrtcName();
  static std::string GetEnvironmentName();
  static http_header_value GetDefaultUserAgent();
};

}  // namespace sora

#endif
