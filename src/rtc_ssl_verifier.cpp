#include "sora/rtc_ssl_verifier.h"

#include <optional>
#include <sstream>
#include <string>

// WebRTC
#include <rtc_base/boringssl_certificate.h>
#include <rtc_base/logging.h>
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

std::string X509ToDebugString(X509* cert) {
  if (!cert) {
    return "nullptr";
  }

  std::ostringstream oss;
  X509_NAME* subject = X509_get_subject_name(cert);
  X509_NAME* issuer = X509_get_issuer_name(cert);

  char subject_str[256] = {0};
  char issuer_str[256] = {0};

  if (subject) {
    X509_NAME_oneline(subject, subject_str, sizeof(subject_str) - 1);
  }
  if (issuer) {
    X509_NAME_oneline(issuer, issuer_str, sizeof(issuer_str) - 1);
  }

  oss << "Subject: " << subject_str << " | Issuer: " << issuer_str;
  return oss.str();
}

void LogCertificateChainComparison(X509* x509_old,
                                   STACK_OF(X509) * x509_chain_old,
                                   X509* x509,
                                   STACK_OF(X509) * x509_chain) {
  RTC_LOG(LS_INFO) << "=== Certificate Verification Debug Info ===";
  auto is_same_cert = [](X509* lhs, X509* rhs) {
    if (lhs == nullptr || rhs == nullptr) {
      return lhs == rhs;
    }
    return X509_cmp(lhs, rhs) == 0;
  };

  // Leaf certificate comparison
  RTC_LOG(LS_INFO) << "Leaf Certificate (x509):";
  RTC_LOG(LS_INFO) << "  Old: " << X509ToDebugString(x509_old);
  RTC_LOG(LS_INFO) << "  New: " << X509ToDebugString(x509);
  RTC_LOG(LS_INFO) << "  Match: " << (is_same_cert(x509_old, x509) ? "yes" : "no");

  // Chain size comparison
  int old_chain_size = x509_chain_old ? sk_X509_num(x509_chain_old) : 0;
  int new_chain_size = x509_chain ? sk_X509_num(x509_chain) : 0;

  RTC_LOG(LS_INFO) << "Certificate Chain Size:";
  RTC_LOG(LS_INFO) << "  Old: " << old_chain_size;
  RTC_LOG(LS_INFO) << "  New: " << new_chain_size;
  RTC_LOG(LS_INFO) << "  Match: " << (old_chain_size == new_chain_size ? "yes" : "no");

  // Chain contents comparison
  if (old_chain_size > 0 && new_chain_size > 0) {
    int min_size = std::min(old_chain_size, new_chain_size);
    for (int i = 0; i < min_size; i++) {
      X509* cert_old = sk_X509_value(x509_chain_old, i);
      X509* cert_new = sk_X509_value(x509_chain, i);

      RTC_LOG(LS_INFO) << "Certificate Chain [" << i << "]:";
      RTC_LOG(LS_INFO) << "  Old: " << X509ToDebugString(cert_old);
      RTC_LOG(LS_INFO) << "  New: " << X509ToDebugString(cert_new);
      RTC_LOG(LS_INFO) << "  Match: "
                       << (is_same_cert(cert_old, cert_new) ? "yes" : "no");
    }
  }

  RTC_LOG(LS_INFO) << "=== End Debug Info ===";
}

}  // namespace

RTCSSLVerifier::RTCSSLVerifier(bool insecure,
                               std::optional<std::string> ca_cert)
    : insecure_(insecure), ca_cert_(ca_cert) {}

bool RTCSSLVerifier::VerifyChain(const webrtc::SSLCertChain& chain) {
  // memo: https://source.chromium.org/chromium/chromium/src/+/main:third_party/webrtc/rtc_base/ssl_certificate.h;l=131-153?q=SSLCertChain&ss=chromium%2Fchromium%2Fsrc:third_party%2Fwebrtc%2F&start=1
  // memo: SSL_get_peer_certificate の実装: https://github.com/google/boringssl/blob/c5e90e2f382678c5f5c05d037367d6029e75710a/ssl/ssl_x509.cc#L372-L383
  // SSL_get_peer_cert_chain: https://github.com/google/boringssl/blob/c5e90e2f382678c5f5c05d037367d6029e75710a/ssl/ssl_x509.cc#L385-L398
  if (insecure_) {
    return true;
  }

  // x509_old と X509_chain_old は以前の実装方法です。
  // これは互換性がある変更ができているかチェックするためにあるコードなので
  // 検証が完了したら削除します。
  SSL* ssl = static_cast<const webrtc::BoringSSLCertificate&>(chain.Get(0)).ssl();
  X509* x509_old = SSL_get_peer_certificate(ssl);
  STACK_OF(X509)* X509_chain_old = SSL_get_peer_cert_chain(ssl);

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
    }
    if (sk_X509_push(x509_chain, cert) == 0) {
      X509_free(cert);
      sk_X509_pop_free(x509_chain, X509_free);
      return false;
    }
  }

  LogCertificateChainComparison(x509_old, X509_chain_old, x509, x509_chain);

  return SSLVerifier::VerifyX509(x509, x509_chain, ca_cert_);
}

}  // namespace sora
