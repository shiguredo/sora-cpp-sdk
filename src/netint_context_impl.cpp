#include "sora/netint_context.h"

#include <memory>

// Netint
#include <ni_device_api.h>
#include <ni_rsrc_api.h>

namespace sora {

namespace {

struct NetintContextImpl : NetintContext {
  NetintContextImpl() = default;
  ~NetintContextImpl();

  bool Initialize();

  bool initialized_ = false;
};

NetintContextImpl::~NetintContextImpl() {
  if (initialized_) {
  }
}

bool NetintContextImpl::Initialize() {
  // Netint リソースマネージャーを初期化
  // should_match_rev=0: リビジョンマッチング不要
  // timeout_seconds=10: 10秒でタイムアウト
  int ret = ni_rsrc_init(0, 10);
  if (ret != NI_RETCODE_SUCCESS) {
    return false;
  }

  initialized_ = true;
  return true;
}

}  // namespace

std::shared_ptr<NetintContext> NetintContext::Create() {
  auto context = std::make_shared<NetintContextImpl>();
  if (!context->Initialize()) {
    return nullptr;
  }
  return context;
}

bool NetintContext::CanCreate() {
  // Netint デバイスが存在するかチェック
  int ret = ni_rsrc_init(0, 10);
  if (ret != NI_RETCODE_SUCCESS) {
    return false;
  }

  // 利用可能なデバイス数を取得
  ni_device_pool_t* device_pool = ni_rsrc_get_device_pool();

  bool has_device = false;
  if (device_pool != nullptr && device_pool->p_device_queue != nullptr) {
    // エンコーダーデバイスがあるか確認
    for (int i = 0; i < NI_MAX_DEVICE_CNT; i++) {
      if (device_pool->p_device_queue->xcoders[NI_DEVICE_TYPE_ENCODER][i] >= 0) {
        has_device = true;
        break;
      }
    }
    ni_rsrc_free_device_pool(device_pool);
  }

  return has_device;
}

}  // namespace sora