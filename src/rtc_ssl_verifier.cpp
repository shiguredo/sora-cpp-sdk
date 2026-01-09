#include "sora/rtc_ssl_verifier.h"

#include <optional>
#include <string>

// WebRTC
#include <rtc_base/ssl_certificate.h>

// OpenSSL
#include <openssl/base.h>
#include <openssl/bio.h>
#include <openssl/pem.h>
#include <openssl/ssl.h>
#include <openssl/stack.h>
#include <openssl/x509.h>

#include "sora/ssl_verifier.h"

namespace sora {

RTCSSLVerifier::RTCSSLVerifier(bool insecure,
                               std::optional<std::string> ca_cert)
    : insecure_(insecure), ca_cert_(ca_cert) {}

bool RTCSSLVerifier::VerifyChain(const webrtc::SSLCertChain& chain) {
  if (insecure_) {
    return true;
  }

  if (chain.GetSize() == 0) {
    return false;
  }

  STACK_OF(X509)* x509_chain = sk_X509_new_null();
  if (!x509_chain) {
    return false;
  }

  X509* x509 = nullptr;

  for (size_t i = 0; i < chain.GetSize(); i++) {
    std::string pem = chain.Get(i).ToPEMString();
    BIO* bio = BIO_new_mem_buf(pem.c_str(), pem.size());
    if (!bio) {
      sk_X509_pop_free(x509_chain, X509_free);
      return false;
    }

    X509* cert = PEM_read_bio_X509(bio, nullptr, nullptr, nullptr);
    BIO_free(bio);

    if (!cert) {
      sk_X509_pop_free(x509_chain, X509_free);
      return false;
    }

    if (i == 0) {
      x509 = cert;
    } else if (sk_X509_push(x509_chain, cert) == 0) {
      X509_free(cert);
      sk_X509_pop_free(x509_chain, X509_free);
      return false;
    }
  }

  return SSLVerifier::VerifyX509(x509, x509_chain, ca_cert_);
}

}  // namespace sora