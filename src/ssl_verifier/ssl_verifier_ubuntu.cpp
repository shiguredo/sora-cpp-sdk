#include "sora/ssl_verifier.h"

// OpenSSL
#include <openssl/bio.h>
#include <openssl/err.h>
#include <openssl/pem.h>
#include <openssl/x509.h>

// WebRTC
#include <rtc_base/logging.h>

#include "ssl_verifier_guard.h"

namespace sora {

bool SSLVerifier::LoadSystemSSLRootCertificates(X509_STORE* store) {
  const char* path = "/etc/ssl/certs/ca-certificates.crt";
  BIO* bio = BIO_new_file(path, "r");
  if (bio == nullptr) {
    RTC_LOG(LS_ERROR)
        << "LoadSystemSSLRootCertificates: BIO_new_file failed: path=" << path;
    return false;
  }
  // BIO の解放は必ず通す
  // bio は以降再代入されない前提で値捕捉する
  Guard bio_guard([bio]() { BIO_free(bio); });

  int added = 0;
  while (true) {
    X509* cert = PEM_read_bio_X509(bio, nullptr, nullptr, nullptr);
    if (cert == nullptr) {
      ERR_get_error();
      break;
    }
    int r = X509_STORE_add_cert(store, cert);
    if (r == 0) {
      ERR_get_error();
      char subject[256] = {0};
      X509_NAME_oneline(X509_get_subject_name(cert), subject, sizeof(subject));
      RTC_LOG(LS_WARNING) << "LoadSystemSSLRootCertificates: "
                             "X509_STORE_add_cert failed: subject="
                          << subject;
    } else {
      ++added;
    }
    X509_free(cert);
  }

  if (added == 0) {
    RTC_LOG(LS_ERROR)
        << "LoadSystemSSLRootCertificates: no certificates loaded: path="
        << path;
    return false;
  }
  return true;
}

}  // namespace sora
