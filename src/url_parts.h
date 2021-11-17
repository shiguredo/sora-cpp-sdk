#ifndef URL_PARTS_H_
#define URL_PARTS_H_

#include <string>

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
};

#endif  // URL_PARTS_H_
