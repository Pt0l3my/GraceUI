// codec.h: the small codecs the server needs, our own and portable: SHA-1 (FIPS 180-4, for Sec-WebSocket-Accept only,
// never for security), Base64 and base64url (RFC 4648), and a UTF-8 validator that can be fed a byte at a time (RFC
// 3629: no overlong form, no surrogate, nothing past U+10FFFF). Pure: no allocation.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

namespace grace::serve {

class Sha1 {
 public:
  void update(std::span<const std::uint8_t> data) noexcept;
  void update(std::string_view text) noexcept;
  // The digest; the object is then spent.
  std::array<std::uint8_t, 20> finish() noexcept;

 private:
  void block(const std::uint8_t* data) noexcept;
  void take(std::uint8_t byte) noexcept;

  std::array<std::uint32_t, 5> state_{0x67452301u, 0xEFCDAB89u, 0x98BADCFEu, 0x10325476u, 0xC3D2E1F0u};
  std::array<std::uint8_t, 64> buffer_{};
  std::size_t buffered_ = 0;
  std::uint64_t length_ = 0;  // in bytes
};

// The encoded size of `bytes` bytes: with padding (Base64), and without it (base64url as tokens use it).
constexpr std::size_t base64_size(std::size_t bytes) noexcept { return (bytes + 2) / 3 * 4; }
constexpr std::size_t base64url_size(std::size_t bytes) noexcept {
  return bytes / 3 * 4 + (bytes % 3 == 0 ? 0 : bytes % 3 + 1);
}

// Base64 (RFC 4648 §4) with padding; returns the characters written, or 0 when `out` is too small for a non-empty input.
std::size_t base64_encode(std::span<const std::uint8_t> in, std::span<char> out) noexcept;
// Strict: a multiple of four characters, '=' only as the last one or two, the unused bits zero, nothing else. Returns
// the bytes written, or nothing.
std::optional<std::size_t> base64_decode(std::string_view in, std::span<std::uint8_t> out) noexcept;

// base64url (RFC 4648 §5) without padding, as strict.
std::size_t base64url_encode(std::span<const std::uint8_t> in, std::span<char> out) noexcept;
std::optional<std::size_t> base64url_decode(std::string_view in, std::span<std::uint8_t> out) noexcept;

// UTF-8, a byte at a time, so that a text message is checked across its fragments (RFC 6455 §8.1).
class Utf8Stream {
 public:
  // False at the first byte that cannot continue valid UTF-8; the stream then stays failed.
  bool feed(std::uint8_t byte) noexcept;
  // At a character's boundary: true at the end of a valid text.
  bool complete() const noexcept { return need_ == 0 && !failed_; }

 private:
  std::uint8_t need_ = 0;  // continuation bytes still to come
  std::uint8_t low_ = 0x80;
  std::uint8_t high_ = 0xBF;  // the next continuation byte's range
  bool failed_ = false;
};

bool valid_utf8(std::span<const std::uint8_t> bytes) noexcept;
bool valid_utf8(std::string_view text) noexcept;

}  // namespace grace::serve
