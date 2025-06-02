#ifndef SORA_SORA_PEER_CONNECTION_FACTORY_H_
#define SORA_SORA_PEER_CONNECTION_FACTORY_H_

// WebRTC
#include <api/peer_connection_interface.h>
#include <api/scoped_refptr.h>
#include <pc/connection_context.h>

namespace sora {

webrtc::scoped_refptr<webrtc::PeerConnectionFactoryInterface>
CreateModularPeerConnectionFactoryWithContext(
    webrtc::PeerConnectionFactoryDependencies dependencies,
    webrtc::scoped_refptr<webrtc::ConnectionContext>& context);

}
#endif