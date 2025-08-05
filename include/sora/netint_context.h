#ifndef SORA_NETINT_CONTEXT_H_
#define SORA_NETINT_CONTEXT_H_

#include <memory>

namespace sora {

struct NetintContext {
  // Netint libxcoder のコンテキストを作成する
  // 対応してない場合やエラーが発生した場合は nullptr を返す
  static std::shared_ptr<NetintContext> Create();

  // Netint が利用可能かチェックする
  static bool CanCreate();
};

}  // namespace sora

#endif