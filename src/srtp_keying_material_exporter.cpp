#include "sora/srtp_keying_material_exporter.h"

#include <p2p/base/dtls_transport.h>
#include <pc/dtls_transport.h>
#include <rtc_base/logging.h>
#include <rtc_base/ssl_stream_adapter.h>

namespace sora {

// Value specified in RFC 5764.
static const char kDtlsSrtpExporterLabel[] = "EXTRACTOR-dtls_srtp";

absl::optional<KeyingMaterial> ExportKeyingMaterial(
    rtc::scoped_refptr<webrtc::PeerConnectionInterface> pc,
    const std::string& mid) {
  rtc::scoped_refptr<webrtc::DtlsTransportInterface> dtls_interface =
      pc->LookupDtlsTransportByMid(mid);
  if (dtls_interface == nullptr) {
    RTC_LOG(LS_ERROR) << "Failed to lookup DtlsTransportInterface";
    return absl::nullopt;
  }

  // shiguredo-webrtc-build/webrtc-build の libwebrtc は
  // RTTI を有効にしてるので dynamic_cast ができる
  webrtc::DtlsTransport* dtls_impl =
      dynamic_cast<webrtc::DtlsTransport*>(dtls_interface.get());
  if (dtls_impl == nullptr) {
    RTC_LOG(LS_ERROR) << "Failed to cast webrtc::DtlsTransport";
    return absl::nullopt;
  }

  cricket::DtlsTransport* dtls =
      dynamic_cast<cricket::DtlsTransport*>(dtls_impl->internal());
  if (dtls == nullptr) {
    RTC_LOG(LS_ERROR) << "Failed to cast cricket::DtlsTransport";
    return absl::nullopt;
  }

  int crypto_suite;
  if (!dtls->GetSrtpCryptoSuite(&crypto_suite)) {
    RTC_LOG(LS_ERROR) << "Failed to get SrtpCryptoSuite";
    return absl::nullopt;
  }

  int key_len;
  int salt_len;
  if (!rtc::GetSrtpKeyAndSaltLengths(crypto_suite, &key_len, &salt_len)) {
    RTC_LOG(LS_ERROR) << "Failed to get SrtpKeyAndSaltLengths";
    return absl::nullopt;
  }

  std::vector<uint8_t> buf(key_len * 2 + salt_len * 2);

  if (!dtls->ExportKeyingMaterial(kDtlsSrtpExporterLabel, nullptr, 0, false,
                                  buf.data(), buf.size())) {
    RTC_LOG(LS_ERROR) << "Failed to ExportKeyingMaterial";
    return absl::nullopt;
  };

  KeyingMaterial km;
  km.client_write_key.resize(key_len);
  km.server_write_key.resize(key_len);
  km.client_write_salt.resize(salt_len);
  km.server_write_salt.resize(salt_len);
  int n = 0;
  memcpy(km.client_write_key.data(), buf.data() + n, key_len);
  n += key_len;
  memcpy(km.server_write_key.data(), buf.data() + n, key_len);
  n += key_len;
  memcpy(km.client_write_salt.data(), buf.data() + n, salt_len);
  n += salt_len;
  memcpy(km.server_write_salt.data(), buf.data() + n, salt_len);
  n += salt_len;

  return km;
}

}  // namespace sora