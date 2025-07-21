#include "sora/rtc_ssl_verifier.h"

// WebRTC
#include <rtc_base/boringssl_certificate.h>

#include "sora/ssl_verifier.h"

namespace sora {

RTCSSLVerifier::RTCSSLVerifier(bool insecure,
                               std::optional<std::string> ca_cert)
    : insecure_(insecure), ca_cert_(ca_cert) {}

bool RTCSSLVerifier::Verify(const webrtc::SSLCertificate& certificate) {
  // insecure の場合は証明書をチェックしない
  if (insecure_) {
    return true;
  }
  SSL* ssl = static_cast<const webrtc::BoringSSLCertificate&>(certificate).ssl();
  X509* x509 = SSL_get_peer_certificate(ssl);
  STACK_OF(X509)* chain = SSL_get_peer_cert_chain(ssl);
  return SSLVerifier::VerifyX509(x509, chain, ca_cert_);
}

}  // namespace sora