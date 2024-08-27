#ifndef SORA_VERSION_H_
#define SORA_VERSION_H_

#include <string>

// Boost
#include <boost/optional.hpp>

namespace sora {

typedef boost::optional<std::string> http_header_value;

class Version {
 public:
  static std::string GetClientName();
  static std::string GetLibwebrtcName();
  static std::string GetEnvironmentName();
  static http_header_value GetDefaultUserAgent();
};

}  // namespace sora

#endif
