#include "sora/sora_peer_connection_factory.h"

#include <utility>

// WebRTC
#include <api/environment/environment_factory.h>
#include <api/make_ref_counted.h>
#include <api/peer_connection_interface.h>
#include <api/scoped_refptr.h>
#include <pc/connection_context.h>
#include <pc/peer_connection_factory.h>
#include <pc/peer_connection_factory_proxy.h>

namespace sora {

// webrtc::PeerConnectionFactory から ConnectionContext を取り出す方法が無いので、
// 継承して無理やり使えるようにする
class PeerConnectionFactoryWithContext : public webrtc::PeerConnectionFactory {
 public:
  PeerConnectionFactoryWithContext(
      webrtc::PeerConnectionFactoryDependencies dependencies)
      : PeerConnectionFactoryWithContext(
            // SDK の外部から webrtc::Environment を設定したくなるまで、ここで初期化する
            webrtc::ConnectionContext::Create(webrtc::CreateEnvironment(),
                                              &dependencies),
            &dependencies) {}
  PeerConnectionFactoryWithContext(
      webrtc::scoped_refptr<webrtc::ConnectionContext> context,
      webrtc::PeerConnectionFactoryDependencies* dependencies)
      : conn_context_(context),
        webrtc::PeerConnectionFactory(webrtc::CreateEnvironment(),
                                      context,
                                      dependencies) {}

  static webrtc::scoped_refptr<PeerConnectionFactoryWithContext> Create(
      webrtc::PeerConnectionFactoryDependencies dependencies) {
    return webrtc::make_ref_counted<PeerConnectionFactoryWithContext>(
        std::move(dependencies));
  }

  webrtc::scoped_refptr<webrtc::ConnectionContext> GetContext() const {
    return conn_context_;
  }

 private:
  webrtc::scoped_refptr<webrtc::ConnectionContext> conn_context_;
};

webrtc::scoped_refptr<webrtc::PeerConnectionFactoryInterface>
CreateModularPeerConnectionFactoryWithContext(
    webrtc::PeerConnectionFactoryDependencies dependencies,
    webrtc::scoped_refptr<webrtc::ConnectionContext>& context) {
  using result_type =
      std::pair<webrtc::scoped_refptr<webrtc::PeerConnectionFactoryInterface>,
                webrtc::scoped_refptr<webrtc::ConnectionContext>>;
  auto p = dependencies.signaling_thread->BlockingCall([&dependencies]() {
    auto factory =
        PeerConnectionFactoryWithContext::Create(std::move(dependencies));
    if (factory == nullptr) {
      return result_type(nullptr, nullptr);
    }
    auto context = factory->GetContext();
    auto proxy = webrtc::PeerConnectionFactoryProxy::Create(
        factory->signaling_thread(), factory->worker_thread(), factory);
    return result_type(proxy, context);
  });
  context = p.second;
  return p.first;
}

}  // namespace sora