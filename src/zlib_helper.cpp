#include "sora/zlib_helper.h"

#include <zlib.h>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <string>

// zlib
#include <chromeconf.h>

namespace sora {

std::string ZlibHelper::Compress(const std::string& input, int level) {
  return Compress((const uint8_t*)input.data(), input.size(), level);
}

std::string ZlibHelper::Compress(const uint8_t* input_buf,
                                 size_t input_size,
                                 int level) {
  std::string output;
  output.resize(16 * 1024);
  uLongf output_size;
  while (true) {
    output_size = output.size();
    int ret = compress2((Bytef*)output.data(), &output_size, input_buf,
                        input_size, level);
    if (ret == Z_BUF_ERROR) {
      output.resize(output.size() * 2);
      continue;
    }
    if (ret != Z_OK) {
      throw std::exception();
    }
    break;
  }
  output.resize(output_size);
  return output;
}

std::string ZlibHelper::Uncompress(const std::string& input) {
  return Uncompress((const uint8_t*)input.data(), input.size());
}

std::string ZlibHelper::Uncompress(const uint8_t* input_buf,
                                   size_t input_size) {
  std::string output;
  output.resize(16 * 1024);
  uLongf output_size;
  while (true) {
    output_size = output.size();
    int ret =
        uncompress((Bytef*)output.data(), &output_size, input_buf, input_size);
    if (ret == Z_BUF_ERROR) {
      output.resize(output.size() * 2);
      continue;
    }
    if (ret != Z_OK) {
      throw std::exception();
    }
    break;
  }
  output.resize(output_size);
  return output;
}

}  // namespace sora
