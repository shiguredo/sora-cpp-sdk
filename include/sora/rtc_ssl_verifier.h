#ifndef RTC_SSL_VERIFIER
#define RTC_SSL_VERIFIER

#include <optional>
#include <string>

// WebRTC
#include <rtc_base/ssl_certificate.h>

namespace sora {

class RTCSSLVerifier : public rtc::SSLCertificateVerifier {
 public:
  RTCSSLVerifier(bool insecure, std::optional<std::string> ca_cert);
  bool Verify(const rtc::SSLCertificate& certificate) override;

 private:
  bool insecure_;
  std::optional<std::string> ca_cert_;
};

}  // namespace sora

#endif
