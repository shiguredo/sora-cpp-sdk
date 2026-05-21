#ifndef SORA_RTC_SSL_VERIFIER_H_
#define SORA_RTC_SSL_VERIFIER_H_

#include <optional>
#include <string>

// WebRTC
#include <rtc_base/ssl_certificate.h>

namespace sora {

class RTCSSLVerifier : public webrtc::SSLCertificateVerifier {
 public:
  RTCSSLVerifier(bool insecure, std::optional<std::string> ca_cert);
  bool VerifyChain(const webrtc::SSLCertChain& chain) override;

 private:
  bool insecure_;
  std::optional<std::string> ca_cert_;
};

}  // namespace sora

#endif
