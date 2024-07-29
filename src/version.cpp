#include "sora/version.h"

#include <fstream>
#include <sstream>
#include <string>
#include <vector>

// boost
#include <boost/algorithm/string/split.hpp>
#include <boost/algorithm/string/trim.hpp>

#ifdef WIN32
#include <windows.h>
#endif

#include "version.gen.h"

#if defined(__APPLE__) || defined(__linux__)
#include <sys/utsname.h>
#endif

#if defined(__APPLE__)
#include "sora/mac/mac_version.h"
#endif

// バージョンやコミットハッシュ情報
// 通常は外から渡すが、渡されていなかった場合の対応
#ifndef SORA_CPP_SDK_VERSION
#define SORA_CPP_SDK_VERSION "internal-build"
#endif

#ifndef SORA_CPP_SDK_COMMIT_SHORT
#define SORA_CPP_SDK_COMMIT_SHORT "unknown"
#endif

#define SORA_NAME \
  "Sora C++ SDK " SORA_CPP_SDK_VERSION " (" SORA_CPP_SDK_COMMIT_SHORT ")"

#if defined(WEBRTC_READABLE_VERSION) && defined(WEBRTC_COMMIT_SHORT) && \
    defined(WEBRTC_BUILD_VERSION)

#define LIBWEBRTC_NAME                                                 \
  "Shiguredo-Build " WEBRTC_READABLE_VERSION " (" WEBRTC_BUILD_VERSION \
  " " WEBRTC_COMMIT_SHORT ")"

#else

#define LIBWEBRTC_NAME "WebRTC custom build"

#endif

#define DEFAULT_USER_AGENT "Mozilla/5.0 (Sora C++ SDK/" SORA_CPP_SDK_VERSION ")"

namespace sora {

std::string Version::GetClientName() {
  return SORA_NAME;
}

std::string Version::GetLibwebrtcName() {
  return LIBWEBRTC_NAME;
}

#ifdef WIN32

static int RtlGetVersion(LPOSVERSIONINFOW lpOSVersionInfo) {
  HMODULE module = GetModuleHandle(L"ntdll.dll");

  if (module != nullptr) {
    typedef int(WINAPI * RtlGetVersionFunc)(LPOSVERSIONINFOW lpOSVersionInfo);

    auto rtl_get_version =
        (RtlGetVersionFunc)GetProcAddress(module, "RtlGetVersion");
    if (rtl_get_version != nullptr) {
      return rtl_get_version(lpOSVersionInfo);
    }
  }

  return -1;
}

#endif

std::string Version::GetEnvironmentName() {
  std::string environment = "Unknown Environment";

#if defined(WIN32)
  SYSTEM_INFO sysInfo;
  GetSystemInfo(&sysInfo);
  std::string arch;
  switch (sysInfo.wProcessorArchitecture) {
    // x64 (AMD or Intel)
    case PROCESSOR_ARCHITECTURE_AMD64:
      arch = "x64";
      break;
    // ARM
    case PROCESSOR_ARCHITECTURE_ARM:
      arch = "arm";
      break;
    // ARM64
    case PROCESSOR_ARCHITECTURE_ARM64:
      arch = "arm64";
      break;
    // Intel Itanium-based
    case PROCESSOR_ARCHITECTURE_IA64:
      arch = "IA64";
      break;
    // x86
    case PROCESSOR_ARCHITECTURE_INTEL:
      arch = "x86";
      break;
    case PROCESSOR_ARCHITECTURE_UNKNOWN:
    default:
      arch = "unknown";
      break;
  }

  OSVERSIONINFOW versionInfo = {sizeof(OSVERSIONINFOW)};
  auto status = RtlGetVersion(&versionInfo);
  std::string os = "Windows <noinfo>";
  if (status == 0) {
    os = "Windows " + std::to_string(versionInfo.dwMajorVersion) + "." +
         std::to_string(versionInfo.dwMinorVersion) + " Build " +
         std::to_string(versionInfo.dwBuildNumber);
  }

  environment = "[" + arch + "] " + os;

#elif defined(__APPLE__) || defined(__linux__)
  std::string arch = "unknown arch";
  std::string os = "Unknown OS";
  std::string info = "";

  utsname u;
  int r = uname(&u);
  if (r == 0) {
    arch = u.machine;
  }

#if defined(__APPLE__)
  os = MacVersion::GetOSName() + " " + MacVersion::GetOSVersion();
#else
  // /etc/os-release ファイルを読んで PRETTY_NAME を利用する

  // /etc/os-release は以下のような内容になっているので、これを適当にパースする
  /*
      $ docker run -it --rm ubuntu cat /etc/os-release
      NAME="Ubuntu"
      VERSION="18.04.3 LTS (Bionic Beaver)"
      ID=ubuntu
      ID_LIKE=debian
      PRETTY_NAME="Ubuntu 18.04.3 LTS"
      VERSION_ID="18.04"
      HOME_URL="https://www.ubuntu.com/"
      SUPPORT_URL="https://help.ubuntu.com/"
      BUG_REPORT_URL="https://bugs.launchpad.net/ubuntu/"
      PRIVACY_POLICY_URL="https://www.ubuntu.com/legal/terms-and-policies/privacy-policy"
      VERSION_CODENAME=bionic
      UBUNTU_CODENAME=bionic
    */
  // 行ごとに分けたデータを取得
  std::vector<std::string> lines;
  {
    std::stringstream ss;
    std::ifstream fin("/etc/os-release");
    ss << fin.rdbuf();
    std::string content = ss.str();
    boost::algorithm::split(lines, ss.str(), boost::is_any_of("\n"));
  }
  const std::string PRETTY_NAME = "PRETTY_NAME=";
  for (auto& line : lines) {
    // 先頭が PRETTY_NAME= の行を探す
    if (line.find(PRETTY_NAME) != 0) {
      continue;
    }
    // PRETTY_NAME= 以降のデータを取り出す
    os = line.substr(PRETTY_NAME.size());
    // 左右の " を除ける（in-place バージョン）
    boost::algorithm::trim_if(os, [](char c) { return c == '"'; });
    break;
  }

#endif

  environment = "[" + arch + "] " + os + info;
#endif

  return environment;
}

http_header_value Version::GetDefaultUserAgent() {
  return std::string(DEFAULT_USER_AGENT);
}

}  // namespace sora
