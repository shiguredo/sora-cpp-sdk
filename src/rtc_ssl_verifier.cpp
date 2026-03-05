#include "sora/rtc_ssl_verifier.h"

#include <memory>
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

namespace {

struct X509Deleter {
  void operator()(X509* p) const { X509_free(p); }
};
struct BIODeleter {
  void operator()(BIO* p) const { BIO_free(p); }
};
struct X509ChainDeleter {
  void operator()(STACK_OF(X509)* p) const { sk_X509_pop_free(p, X509_free); }
};

using X509Ptr = std::unique_ptr<X509, X509Deleter>;
using BIOPtr = std::unique_ptr<BIO, BIODeleter>;
using X509ChainPtr = std::unique_ptr<STACK_OF(X509), X509ChainDeleter>;

X509Ptr ToX509(const webrtc::SSLCertificate& certificate) {
  std::string pem = certificate.ToPEMString();
  BIOPtr bio(BIO_new_mem_buf(pem.c_str(), pem.size()));
  if (!bio) {
    return X509Ptr(nullptr);
  }
  return X509Ptr(PEM_read_bio_X509(bio.get(), nullptr, nullptr, nullptr));
}

}  // namespace

RTCSSLVerifier::RTCSSLVerifier(bool insecure,
                               std::optional<std::string> ca_cert)
    : insecure_(insecure), ca_cert_(ca_cert) {}

bool RTCSSLVerifier::VerifyChain(const webrtc::SSLCertChain& chain) {
  if (insecure_) {
    return true;
  }

  X509ChainPtr x509_chain(sk_X509_new_null());
  if (!x509_chain) {
    return false;
  }

  // VerifyX509 用に leaf と intermediate を分けて構築する。
  X509Ptr x509 =
      chain.GetSize() > 0 ? ToX509(chain.Get(0)) : X509Ptr(nullptr);
  if (!x509) {
    return false;
  }

  for (size_t i = 1; i < chain.GetSize(); i++) {
    X509Ptr cert = ToX509(chain.Get(i));
    if (!cert) {
      return false;
    }
    if (sk_X509_push(x509_chain.get(), cert.get()) == 0) {
      return false;
    }
    cert.release();
  }

  return SSLVerifier::VerifyX509(x509.get(), x509_chain.get(), ca_cert_);
}

}  // namespace sora
