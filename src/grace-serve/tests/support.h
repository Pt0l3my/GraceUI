// support.h: what grace-serve's tests share: the listeners, heads built from parts, a page's masked frames, and feeding
// bytes in the pieces a test asks for.
#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "grace/serve/head.h"
#include "grace/serve/websocket.h"

namespace grace::serve::test {

inline constexpr std::uint16_t kPort = 8642;
// Documentation addresses (RFC 5737, RFC 3849): a network listener's own, invented.
inline constexpr std::string_view kNetworkAddresses[] = {"192.0.2.7", "[2001:db8::7]"};

inline HeadRules loopback_rules() {
  HeadRules rules;
  rules.listener.port = kPort;
  return rules;
}

inline HeadRules network_rules() {
  HeadRules rules;
  rules.listener.network = true;
  rules.listener.port = kPort;
  rules.listener.addresses = kNetworkAddresses;
  return rules;
}

// A GET of `target` on 127.0.0.1:8642 with more field lines, each ending in CRLF.
inline std::string get_head(std::string_view target = "/", std::string_view fields = "") {
  return "GET " + std::string(target) + " HTTP/1.1\r\nHost: 127.0.0.1:8642\r\n" + std::string(fields) + "\r\n";
}

inline std::vector<std::uint8_t> bytes_of(std::string_view text) {
  std::vector<std::uint8_t> bytes;
  for (const char c : text) {
    bytes.push_back(static_cast<std::uint8_t>(c));
  }
  return bytes;
}

inline std::string text_of(std::span<const std::uint8_t> bytes) {
  std::string text;
  for (const std::uint8_t byte : bytes) {
    text.push_back(static_cast<char>(byte));
  }
  return text;
}

// The length forms a page's frame may be written in: the minimal one, or a longer one that must be refused.
enum class LengthForm { minimal, sixteen, sixty_four };

struct FrameSpec {
  bool fin = true;
  std::uint8_t opcode = 1;
  std::vector<std::uint8_t> payload;
  bool masked = true;
  std::uint8_t rsv = 0;  // the RSV bits, already in place (0x40, 0x20, 0x10)
  LengthForm form = LengthForm::minimal;
  std::array<std::uint8_t, 4> mask{0x37, 0xFA, 0x21, 0x3D};  // RFC 6455 §5.7's
  std::uint64_t claimed = UINT64_MAX;  // a length to write other than the payload's
};

// A page's frame, as a browser would send it.
inline std::vector<std::uint8_t> frame(const FrameSpec& spec) {
  std::vector<std::uint8_t> out;
  out.push_back(static_cast<std::uint8_t>((spec.fin ? 0x80 : 0) | spec.rsv | spec.opcode));
  const std::uint64_t length = spec.claimed != UINT64_MAX ? spec.claimed : spec.payload.size();
  const std::uint8_t mask_bit = spec.masked ? 0x80 : 0;
  LengthForm form = spec.form;
  if (form == LengthForm::minimal) {
    form = length < 126 ? LengthForm::minimal : length <= 0xFFFF ? LengthForm::sixteen : LengthForm::sixty_four;
  }
  if (form == LengthForm::minimal) {
    out.push_back(static_cast<std::uint8_t>(mask_bit | length));
  } else if (form == LengthForm::sixteen) {
    out.push_back(static_cast<std::uint8_t>(mask_bit | 126));
    out.push_back(static_cast<std::uint8_t>(length >> 8));
    out.push_back(static_cast<std::uint8_t>(length));
  } else {
    out.push_back(static_cast<std::uint8_t>(mask_bit | 127));
    for (int shift = 56; shift >= 0; shift -= 8) {
      out.push_back(static_cast<std::uint8_t>(length >> shift));
    }
  }
  if (spec.masked) {
    out.insert(out.end(), spec.mask.begin(), spec.mask.end());
  }
  for (std::size_t i = 0; i < spec.payload.size(); ++i) {
    out.push_back(spec.masked ? static_cast<std::uint8_t>(spec.payload[i] ^ spec.mask[i % 4]) : spec.payload[i]);
  }
  return out;
}

inline std::vector<std::uint8_t> text_frame(std::string_view text, bool fin = true, std::uint8_t opcode = 1) {
  FrameSpec spec;
  spec.fin = fin;
  spec.opcode = opcode;
  spec.payload = bytes_of(text);
  return frame(spec);
}

inline std::vector<std::uint8_t> join(std::initializer_list<std::vector<std::uint8_t>> parts) {
  std::vector<std::uint8_t> out;
  for (const auto& part : parts) {
    out.insert(out.end(), part.begin(), part.end());
  }
  return out;
}

// Split points: whole (empty), a byte at a time (all ones), or the sizes given, repeated.
inline std::vector<std::size_t> pieces(std::size_t total, const std::vector<std::size_t>& sizes) {
  std::vector<std::size_t> out;
  if (sizes.empty()) {
    out.push_back(total);
    return out;
  }
  std::size_t at = 0;
  for (std::size_t i = 0; at < total; ++i) {
    const std::size_t size = std::min(sizes[i % sizes.size()], total - at);
    out.push_back(size);
    at += size;
  }
  return out;
}

}  // namespace grace::serve::test
