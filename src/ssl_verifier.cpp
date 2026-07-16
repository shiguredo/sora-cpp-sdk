#include "sora/ssl_verifier.h"

#include <cstddef>
#include <functional>
#include <optional>
#include <string>
#include <utility>

// WebRTC
#include <rtc_base/logging.h>

// OpenSSL
#include <openssl/base.h>
#include <openssl/bio.h>
#include <openssl/err.h>
#include <openssl/pem.h>
#include <openssl/stack.h>
#include <openssl/x509.h>

namespace sora {

bool SSLVerifier::AddCert(const std::string& pem, X509_STORE* store) {
  BIO* bio = BIO_new_mem_buf(pem.c_str(), pem.size());
  if (bio == nullptr) {
    RTC_LOG(LS_ERROR) << "BIO_new_mem_buf failed";
    return false;
  }
  while (true) {
    X509* cert = PEM_read_bio_X509(bio, nullptr, nullptr, nullptr);
    if (cert == nullptr) {
      ERR_get_error();
      break;
    }

    int r = X509_STORE_add_cert(store, cert);
    if (r == 0) {
      X509_free(cert);
      BIO_free(bio);
      RTC_LOG(LS_ERROR) << "X509_STORE_add_cert failed";
      return false;
    }
    X509_free(cert);
  }

  BIO_free(bio);
  return true;
}

bool SSLVerifier::VerifyX509(X509* x509,
                             STACK_OF(X509) * chain,
                             const std::optional<std::string>& ca_cert) {
  {
    char data[256];
    RTC_LOG(LS_INFO) << "cert:";
    X509_NAME_oneline(X509_get_subject_name(x509), data, sizeof(data));
    RTC_LOG(LS_INFO) << "  subject = " << data;
    X509_NAME_oneline(X509_get_issuer_name(x509), data, sizeof(data));
    RTC_LOG(LS_INFO) << "  issuer  = " << data;

    if (chain != nullptr) {
      int n = sk_X509_num(chain);
      for (int i = 0; i < n; i++) {
        X509* x = sk_X509_value(chain, i);
        RTC_LOG(LS_INFO) << "chain[" << i << "]:";
        X509_NAME_oneline(X509_get_subject_name(x), data, sizeof(data));
        RTC_LOG(LS_INFO) << "  subject = " << data;
        X509_NAME_oneline(X509_get_issuer_name(x), data, sizeof(data));
        RTC_LOG(LS_INFO) << "  issuer  = " << data;
      }
    }
  }

  X509_STORE* store = nullptr;
  X509_STORE_CTX* ctx = nullptr;

  struct Guard {
    std::function<void()> f;
    Guard(std::function<void()> f) : f(std::move(f)) {}
    ~Guard() { f(); }
  };
  Guard guard([&]() {
    // nullptr を渡しても何もしない
    X509_STORE_CTX_free(ctx);
    X509_STORE_free(store);
  });

  store = X509_STORE_new();
  if (store == nullptr) {
    RTC_LOG(LS_ERROR) << "X509_STORE_new failed";
    return false;
  }
  int r;
  r = X509_STORE_set_flags(store, X509_V_FLAG_TRUSTED_FIRST);
  if (r == 0) {
    RTC_LOG(LS_ERROR) << "X509_STORE_set_flags failed";
    return false;
  }

  if (!ca_cert) {
    // OS のシステム CA を信頼ストアに追加する
    if (!LoadSystemSSLRootCertificates(store)) {
      RTC_LOG(LS_ERROR) << "LoadSystemSSLRootCertificates failed";
      return false;
    }
  } else {
    // ルート証明書が指定されている場合、その証明書以外は読み込まない
    if (!AddCert(*ca_cert, store)) {
      RTC_LOG(LS_ERROR) << "Failed to add ca_cert: ca_cert_length="
                        << ca_cert->size();
      return false;
    }
  }
  ctx = X509_STORE_CTX_new();
  if (ctx == nullptr) {
    RTC_LOG(LS_ERROR) << "X509_STORE_CTX_new failed";
    return false;
  }
  r = X509_STORE_CTX_init(ctx, store, x509, chain);
  if (r == 0) {
    RTC_LOG(LS_ERROR) << "X509_STORE_CTX_init failed";
    return false;
  }
  r = X509_verify_cert(ctx);
  if (r <= 0) {
    RTC_LOG(LS_INFO) << "X509_verify_cert failed: r=" << r << " message="
                     << X509_verify_cert_error_string(
                            X509_STORE_CTX_get_error(ctx));
    return false;
  }
  return true;
}

}  // namespace sora
