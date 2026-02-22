#ifndef SORA_ZLIB_HELPER_H_
#define SORA_ZLIB_HELPER_H_

#include <cstddef>
#include <cstdint>
#include <string>

// zlib
#include <chromeconf.h>
#include <zlib.h>

// この define 名が邪魔してるので undef しておく
#undef compress

namespace sora {

class ZlibHelper {
 public:
  static std::string Compress(const std::string& input,
                              int level = Z_DEFAULT_COMPRESSION);
  static std::string Compress(const uint8_t* input_buf,
                              size_t input_size,
                              int level = Z_DEFAULT_COMPRESSION);

  static std::string Uncompress(const std::string& input);
  static std::string Uncompress(const uint8_t* input_buf, size_t input_size);
};

}  // namespace sora

#endif