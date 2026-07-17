#include "sora/ssl_verifier.h"

#include <dirent.h>
#include <errno.h>

#include <functional>
#include <string>
#include <utility>

#include <openssl/bio.h>
#include <openssl/err.h>
#include <openssl/pem.h>
#include <openssl/x509.h>

#include <rtc_base/logging.h>

namespace sora {
namespace {

// 単一ディレクトリを走査してストアに追加、追加件数を返す。
// opendir 失敗時は errno == ENOENT なら無音で 0（Android バージョンで片方の経路が無いケース）、
// それ以外は WARNING を出して 0 を返す
int LoadFromDir(X509_STORE* store, const char* dir_path) {
  struct Guard {
    std::function<void()> f;
    Guard(std::function<void()> f) : f(std::move(f)) {}
    ~Guard() { f(); }
  };

  DIR* dir = opendir(dir_path);
  if (dir == nullptr) {
    int e = errno;
    if (e != ENOENT) {
      RTC_LOG(LS_WARNING)
          << "LoadSystemSSLRootCertificates: opendir failed: path=" << dir_path
          << " errno=" << e;
    }
    return 0;
  }
  Guard dir_guard([dir]() { closedir(dir); });

  int added = 0;
  struct dirent* entry;
  while ((entry = readdir(dir)) != nullptr) {
    if (entry->d_name[0] == '.') {
      // AOSP CA ファイルは <subject_hash>.<n> 命名でドット始まりを含まないため
      // "." / ".." およびドット始まりの隠しファイルは対象外で安全
      continue;
    }
    std::string path = std::string(dir_path) + "/" + entry->d_name;
    BIO* bio = BIO_new_file(path.c_str(), "r");
    if (bio == nullptr) {
      ERR_get_error();
      RTC_LOG(LS_WARNING)
          << "LoadSystemSSLRootCertificates: BIO_new_file failed: path=" << path;
      continue;
    }
    Guard bio_guard([bio]() { BIO_free(bio); });

    while (true) {
      X509* cert = PEM_read_bio_X509(bio, nullptr, nullptr, nullptr);
      if (cert == nullptr) {
        ERR_get_error();
        break;
      }
      int r = X509_STORE_add_cert(store, cert);
      if (r == 0) {
        // 重複拒否する版の BoringSSL では X509_R_CERT_ALREADY_IN_HASH_TABLE が積まれる。
        // この場合はエラーキューから 1 件取り出すのみで WARNING は出さない。
        // 他 reason（allocation 失敗等）は WARNING を出す
        unsigned long err = ERR_peek_last_error();
        if (ERR_GET_REASON(err) == X509_R_CERT_ALREADY_IN_HASH_TABLE) {
          ERR_get_error();
        } else {
          char subject[256] = {0};
          X509_NAME_oneline(X509_get_subject_name(cert), subject, sizeof(subject));
          RTC_LOG(LS_WARNING)
              << "LoadSystemSSLRootCertificates: X509_STORE_add_cert failed: file="
              << entry->d_name << " subject=" << subject;
          ERR_get_error();
        }
      } else {
        ++added;
      }
      X509_free(cert);
    }
  }

  return added;
}

}  // namespace

bool SSLVerifier::LoadSystemSSLRootCertificates(X509_STORE* store) {
  // Conscrypt Mainline module 経由の更新可能な CA ストア（Android 14 以降で提供）を優先
  int added_apex = LoadFromDir(store, "/apex/com.android.conscrypt/cacerts");
  // AOSP 標準の system パス（Android 10-13 の主要ストア、Android 14+ でも残る）
  int added_system = LoadFromDir(store, "/system/etc/security/cacerts");
  int added = added_apex + added_system;

  if (added == 0) {
    RTC_LOG(LS_ERROR)
        << "LoadSystemSSLRootCertificates: no certificates loaded: added=0"
        << " apex=" << added_apex << " system=" << added_system;
    return false;
  }
  RTC_LOG(LS_INFO)
      << "LoadSystemSSLRootCertificates: added=" << added
      << ", apex=" << added_apex << ", system=" << added_system;
  return true;
}

}  // namespace sora
