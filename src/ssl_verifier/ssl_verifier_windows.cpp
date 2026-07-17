#include "sora/ssl_verifier.h"

// Windows CryptoAPI
#include <windows.h>
#include <wincrypt.h>

// OpenSSL
#include <openssl/err.h>
#include <openssl/x509.h>

// WebRTC
#include <rtc_base/logging.h>

#include "ssl_verifier_util.h"

namespace sora {

bool LoadSystemSSLRootCertificates(X509_STORE* store) {
  // 第 1 引数 hProv は MSDN 仕様どおり NULL を渡す（本引数は使用されない）
  // 第 2 引数 L"ROOT" は Windows の「信頼されたルート証明機関」ストア
  HCERTSTORE h_store = CertOpenSystemStoreW(NULL, L"ROOT");
  if (h_store == NULL) {
    // GetLastError は後続の Win32 呼び出しで上書きされる可能性があるため即時に取得する
    DWORD err = GetLastError();
    RTC_LOG(LS_ERROR) << "LoadSystemSSLRootCertificates: CertOpenSystemStoreW "
                         "failed: last_error="
                      << err;
    return false;
  }
  Guard store_guard([h_store]() { CertCloseStore(h_store, 0); });

  int added = 0;
  // CertEnumCertificatesInStore は次回呼び出しで前回の PCCERT_CONTEXT を
  // 自動解放するため、ループ中の CertFreeCertificateContext は呼ばない
  PCCERT_CONTEXT ctx = nullptr;
  while ((ctx = CertEnumCertificatesInStore(h_store, ctx)) != nullptr) {
    // dwCertEncodingType には通常 X509_ASN_ENCODING (0x1) のみが入るが、
    // 将来の CryptoAPI 拡張で他エンコーディングが混ざった場合の防御としてビット判定する
    if ((ctx->dwCertEncodingType & X509_ASN_ENCODING) == 0) {
      continue;
    }
    const unsigned char* p = ctx->pbCertEncoded;
    // d2i_X509 は戻り時点で pbCertEncoded のバイト列のパースを完了しているため、
    // 以降 ctx（および ctx が指すバイト列）の寿命は気にしなくてよい
    X509* cert = d2i_X509(nullptr, &p, static_cast<long>(ctx->cbCertEncoded));
    if (cert == nullptr) {
      // d2i_X509 失敗でエラーキューが積まれるため 1 回取り出してクリアする（現行 AddCert と同型）
      ERR_get_error();
      RTC_LOG(LS_WARNING) << "LoadSystemSSLRootCertificates: d2i_X509 failed";
      continue;
    }
    int r = X509_STORE_add_cert(store, cert);
    if (r == 0) {
      char subject[256] = {0};
      // subject が 256 バイト超なら切り詰められるが、X509_NAME_oneline は NUL 終端保証あり
      X509_NAME_oneline(X509_get_subject_name(cert), subject, sizeof(subject));
      RTC_LOG(LS_WARNING) << "LoadSystemSSLRootCertificates: "
                             "X509_STORE_add_cert failed: subject="
                          << subject;
      // ROOT ストアの 5 経路仮想ビューでは同一 CA が繰り返し追加を試みられ、
      // その都度 X509_R_CERT_ALREADY_IN_HASH_TABLE 等のエラーがキューに積まれるため
      // 次イテレーションの d2i_X509 / X509_STORE_add_cert のエラー報告を汚染しないよう
      // 1 回取り出してクリアする
      ERR_get_error();
    } else {
      ++added;
    }
    X509_free(cert);
  }
  // ループを抜けた時点で ctx == nullptr。MSDN 仕様上 nullptr は「列挙完了」と
  // 「途中エラー」の両方を意味するため GetLastError() で識別する
  DWORD enum_last_error = GetLastError();
  if (enum_last_error != CRYPT_E_NOT_FOUND) {
    RTC_LOG(LS_WARNING)
        << "LoadSystemSSLRootCertificates: CertEnumCertificatesInStore ended "
           "abnormally: last_error="
        << enum_last_error;
  }

  if (added == 0) {
    RTC_LOG(LS_ERROR) << "LoadSystemSSLRootCertificates: no certificates "
                         "loaded from Windows ROOT store";
    return false;
  }
  RTC_LOG(LS_INFO) << "LoadSystemSSLRootCertificates: added=" << added;
  return true;
}

}  // namespace sora
