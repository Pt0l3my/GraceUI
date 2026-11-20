// handshake.cpp: the WebSocket upgrade's check and Sec-WebSocket-Accept (RFC 6455 §4.2; websocket.h).
#include <array>

#include "ascii.h"
#include "grace/serve/websocket.h"

namespace grace::serve {
namespace {

// RFC 6455 §1.3.
constexpr std::string_view kGuid = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
constexpr std::size_t kNonceBytes = 16;

}  // namespace

void compute_accept(std::string_view key, std::span<char, kAcceptChars> accept) noexcept {
  Sha1 sha;
  sha.update(key);
  sha.update(kGuid);
  const std::array<std::uint8_t, 20> digest = sha.finish();
  base64_encode(digest, accept);
}

Refusal check_upgrade(const RequestHead& head, std::size_t bytes_after_head,
                      std::span<char, kAcceptChars> accept) noexcept {
  if (head.method != Method::get) {
    return Refusal::upgrade_not_get;
  }
  if (!head.upgrade) {
    return Refusal::upgrade_required;
  }
  if (!detail::iequals(*head.upgrade, "websocket")) {
    return Refusal::bad_upgrade;
  }
  if (!head.has_token("connection", "upgrade")) {
    return Refusal::no_connection_upgrade;
  }
  if (!head.sec_websocket_version) {
    return Refusal::ws_version_missing;
  }
  if (*head.sec_websocket_version != "13") {
    return Refusal::ws_version;
  }
  // Room for more than the nonce, so that a longer key is seen to be one.
  std::array<std::uint8_t, kNonceBytes + 2> nonce{};
  const std::optional<std::size_t> decoded =
      head.sec_websocket_key ? base64_decode(*head.sec_websocket_key, nonce) : std::nullopt;
  if (!decoded || *decoded != kNonceBytes) {
    return Refusal::bad_ws_key;
  }
  if (head.content_length != 0 || bytes_after_head != 0) {
    return Refusal::upgrade_with_body;
  }
  compute_accept(*head.sec_websocket_key, accept);
  return Refusal::none;
}

}  // namespace grace::serve
