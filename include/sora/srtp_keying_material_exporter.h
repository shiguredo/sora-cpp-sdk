#ifndef SORA_SRTP_KEYING_MATERIAL_EXPORTER_H_
#define SORA_SRTP_KEYING_MATERIAL_EXPORTER_H_

#include <string>

#include <absl/types/optional.h>
#include <api/peer_connection_interface.h>

namespace sora {

struct KeyingMaterial {
  std::vector<uint8_t> client_write_key;
  std::vector<uint8_t> server_write_key;
  std::vector<uint8_t> client_write_salt;
  std::vector<uint8_t> server_write_salt;
};

absl::optional<KeyingMaterial> ExportKeyingMaterial(
    rtc::scoped_refptr<webrtc::PeerConnectionInterface> pc,
    const std::string& mid);

}  // namespace sora

#endif
