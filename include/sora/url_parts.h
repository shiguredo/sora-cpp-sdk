#ifndef SORA_URL_PARTS_H_
#define SORA_URL_PARTS_H_

#include <string>

namespace sora {

// 適当な URL パーサ
struct URLParts {
  std::string scheme;
  std::string user_pass;
  std::string host;
  std::string port;
  std::string path_query_fragment;

  // 適当 URL パース
  // scheme://[user_pass@]host[:port][/path_query_fragment]
  static bool Parse(std::string url, URLParts& parts);

  // port を返すが、特に指定されていなかった場合、
  // scheme が https/wss の場合は 443、それ以外の場合は 80 を返す
  std::string GetPort() const;
};

}  // namespace sora

#endif
