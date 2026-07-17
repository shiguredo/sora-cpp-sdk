#ifndef SORA_SSL_VERIFIER_GUARD_H_
#define SORA_SSL_VERIFIER_GUARD_H_

#include <functional>
#include <utility>

// OpenSSL
#include <openssl/ssl.h>
#include <openssl/stack.h>
#include <openssl/x509.h>

// WebRTC
#include <rtc_base/logging.h>

namespace sora {

// スコープ終了時に任意の処理を実行する RAII ガード
struct Guard {
  std::function<void()> f;
  Guard(std::function<void()> f) : f(std::move(f)) {}
  ~Guard() { f(); }
};

// 証明書とチェーンの subject / issuer を INFO ログに出力する
inline void DumpX509CertificateInfo(X509* x509, STACK_OF(X509) * chain) {
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

}  // namespace sora

#endif
