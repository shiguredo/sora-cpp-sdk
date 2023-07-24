#include "sora/sora_peer_connection_factory.h"

// WebRTC
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
            webrtc::ConnectionContext::Create(&dependencies),
            &dependencies) {}
  PeerConnectionFactoryWithContext(
      rtc::scoped_refptr<webrtc::ConnectionContext> context,
      webrtc::PeerConnectionFactoryDependencies* dependencies)
      : conn_context_(context),
        webrtc::PeerConnectionFactory(context, dependencies) {}

  static rtc::scoped_refptr<PeerConnectionFactoryWithContext> Create(
      webrtc::PeerConnectionFactoryDependencies dependencies) {
    return rtc::make_ref_counted<PeerConnectionFactoryWithContext>(
        std::move(dependencies));
  }

  rtc::scoped_refptr<webrtc::ConnectionContext> GetContext() const {
    return conn_context_;
  }

 private:
  rtc::scoped_refptr<webrtc::ConnectionContext> conn_context_;
};

rtc::scoped_refptr<webrtc::PeerConnectionFactoryInterface>
CreateModularPeerConnectionFactoryWithContext(
    webrtc::PeerConnectionFactoryDependencies dependencies,
    rtc::scoped_refptr<webrtc::ConnectionContext>& context) {
  using result_type =
      std::pair<rtc::scoped_refptr<webrtc::PeerConnectionFactoryInterface>,
                rtc::scoped_refptr<webrtc::ConnectionContext>>;
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