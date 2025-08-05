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
    ni_rsrc_free_device_context();
  }
}

bool NetintContextImpl::Initialize() {
  // Netint リソースマネージャーを初期化
  ni_retcode_t ret = ni_rsrc_init();
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
  ni_retcode_t ret = ni_rsrc_init();
  if (ret != NI_RETCODE_SUCCESS) {
    return false;
  }

  // 利用可能なデバイス数を取得
  ni_device_queue_t* device_queue = nullptr;
  ret = ni_rsrc_get_available_devices(&device_queue, NI_DEVICE_TYPE_ENCODER);

  bool has_device = false;
  if (ret == NI_RETCODE_SUCCESS && device_queue != nullptr) {
    has_device = device_queue->length > 0;
    ni_rsrc_free_device_queue(device_queue);
  }

  ni_rsrc_free_device_context();
  return has_device;
}

}  // namespace sora