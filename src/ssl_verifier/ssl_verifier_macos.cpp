#include "sora/ssl_verifier.h"

#include <CoreFoundation/CoreFoundation.h>
#include <Security/Security.h>

// OpenSSL
#include <openssl/err.h>
#include <openssl/x509.h>

// WebRTC
#include <rtc_base/logging.h>

#include "ssl_verifier_guard.h"

namespace sora {

bool SSLVerifier::LoadSystemSSLRootCertificates(X509_STORE* store) {
  CFArrayRef anchors = nullptr;
  OSStatus status = SecTrustCopyAnchorCertificates(&anchors);
  if (status != errSecSuccess || anchors == nullptr) {
    RTC_LOG(LS_ERROR) << "LoadSystemSSLRootCertificates: "
                         "SecTrustCopyAnchorCertificates failed: status="
                      << status;
    return false;
  }
  Guard anchors_guard([anchors]() { CFRelease(anchors); });

  int added = 0;
  CFIndex count = CFArrayGetCount(anchors);
  for (CFIndex i = 0; i < count; ++i) {
    // CFArrayGetValueAtIndex は Get 系のため個別 CFRelease は不要
    SecCertificateRef sec_cert = static_cast<SecCertificateRef>(
        const_cast<void*>(CFArrayGetValueAtIndex(anchors, i)));
    if (sec_cert == nullptr) {
      continue;
    }
    // SecCertificateCopyData は Copy 系のため CFRelease する
    CFDataRef der = SecCertificateCopyData(sec_cert);
    if (der == nullptr) {
      RTC_LOG(LS_WARNING)
          << "LoadSystemSSLRootCertificates: SecCertificateCopyData failed";
      continue;
    }
    const unsigned char* p =
        reinterpret_cast<const unsigned char*>(CFDataGetBytePtr(der));
    // d2i_X509 は der のバイト列を参照して X509 を構築するため、der の解放は d2i_X509 が返るまで遅らせる
    // CFDataGetLength は CFIndex（long）を返すが d2i_X509 の第 3 引数は long なので static_cast で明示する
    X509* cert = d2i_X509(nullptr, &p, static_cast<long>(CFDataGetLength(der)));
    CFRelease(der);
    if (cert == nullptr) {
      // d2i_X509 失敗でエラーキューが積まれるため 1 回取り出してクリアする（現行 AddCert と同型）
      ERR_get_error();
      RTC_LOG(LS_WARNING) << "LoadSystemSSLRootCertificates: d2i_X509 failed";
      continue;
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
    RTC_LOG(LS_ERROR) << "LoadSystemSSLRootCertificates: no certificates "
                         "loaded from Keychain System Roots: count="
                      << count;
    return false;
  }
  RTC_LOG(LS_INFO) << "LoadSystemSSLRootCertificates: added=" << added;
  return true;
}

}  // namespace sora
