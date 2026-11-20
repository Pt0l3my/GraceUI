// codec.cpp: SHA-1 (FIPS 180-4 §6.1), Base64 and base64url (RFC 4648 §4, §5) and UTF-8 (RFC 3629 §4), our own.
#include "grace/serve/codec.h"

namespace grace::serve {
namespace {

constexpr std::string_view kBase64 = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
constexpr std::string_view kBase64Url = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";

constexpr std::uint32_t rotl(std::uint32_t value, unsigned bits) noexcept {
  return (value << bits) | (value >> (32 - bits));
}

std::size_t encode(std::span<const std::uint8_t> in, std::span<char> out, std::string_view alphabet,
                   bool pad) noexcept {
  const std::size_t size = pad ? base64_size(in.size()) : base64url_size(in.size());
  if (out.size() < size) {
    return 0;
  }
  std::size_t written = 0;
  std::size_t at = 0;
  for (; at + 3 <= in.size(); at += 3) {
    const std::uint32_t group = (std::uint32_t{in[at]} << 16) | (std::uint32_t{in[at + 1]} << 8) | in[at + 2];
    out[written++] = alphabet[(group >> 18) & 63];
    out[written++] = alphabet[(group >> 12) & 63];
    out[written++] = alphabet[(group >> 6) & 63];
    out[written++] = alphabet[group & 63];
  }
  const std::size_t rest = in.size() - at;
  if (rest != 0) {
    const std::uint32_t group = (std::uint32_t{in[at]} << 16) | (rest == 2 ? std::uint32_t{in[at + 1]} << 8 : 0u);
    out[written++] = alphabet[(group >> 18) & 63];
    out[written++] = alphabet[(group >> 12) & 63];
    if (rest == 2) {
      out[written++] = alphabet[(group >> 6) & 63];
    } else if (pad) {
      out[written++] = '=';
    }
    if (pad) {
      out[written++] = '=';
    }
  }
  return written;
}

constexpr int value_of(char c, bool url) noexcept {
  if (c >= 'A' && c <= 'Z') {
    return c - 'A';
  }
  if (c >= 'a' && c <= 'z') {
    return c - 'a' + 26;
  }
  if (c >= '0' && c <= '9') {
    return c - '0' + 52;
  }
  if (c == (url ? '-' : '+')) {
    return 62;
  }
  if (c == (url ? '_' : '/')) {
    return 63;
  }
  return -1;
}

std::optional<std::size_t> decode(std::string_view in, std::span<std::uint8_t> out, bool url) noexcept {
  std::size_t data = in.size();
  if (!url) {
    if (in.size() % 4 != 0) {
      return std::nullopt;
    }
    if (data != 0 && in[data - 1] == '=') {
      --data;
      if (in[data - 1] == '=') {
        --data;
      }
    }
  }
  if (data % 4 == 1) {
    return std::nullopt;
  }
  const std::size_t size = data / 4 * 3 + (data % 4 == 0 ? 0 : data % 4 - 1);
  if (out.size() < size) {
    return std::nullopt;
  }
  std::uint32_t bits = 0;
  unsigned held = 0;
  std::size_t written = 0;
  for (std::size_t i = 0; i < data; ++i) {
    const int value = value_of(in[i], url);
    if (value < 0) {
      return std::nullopt;
    }
    bits = (bits << 6) | static_cast<std::uint32_t>(value);
    held += 6;
    if (held >= 8) {
      held -= 8;
      out[written++] = static_cast<std::uint8_t>(bits >> held);
      bits &= (1u << held) - 1;
    }
  }
  if (bits != 0) {
    return std::nullopt;  // the unused bits of the last character must be zero: one encoding per value
  }
  return written;
}

}  // namespace

void Sha1::block(const std::uint8_t* data) noexcept {
  std::array<std::uint32_t, 80> words{};
  for (std::size_t i = 0; i < 16; ++i) {
    words[i] = (std::uint32_t{data[4 * i]} << 24) | (std::uint32_t{data[4 * i + 1]} << 16) |
               (std::uint32_t{data[4 * i + 2]} << 8) | std::uint32_t{data[4 * i + 3]};
  }
  for (std::size_t i = 16; i < 80; ++i) {
    words[i] = rotl(words[i - 3] ^ words[i - 8] ^ words[i - 14] ^ words[i - 16], 1);
  }
  std::uint32_t a = state_[0];
  std::uint32_t b = state_[1];
  std::uint32_t c = state_[2];
  std::uint32_t d = state_[3];
  std::uint32_t e = state_[4];
  for (std::size_t i = 0; i < 80; ++i) {
    std::uint32_t f = 0;
    std::uint32_t k = 0;
    if (i < 20) {
      f = (b & c) | (~b & d);
      k = 0x5A827999u;
    } else if (i < 40) {
      f = b ^ c ^ d;
      k = 0x6ED9EBA1u;
    } else if (i < 60) {
      f = (b & c) | (b & d) | (c & d);
      k = 0x8F1BBCDCu;
    } else {
      f = b ^ c ^ d;
      k = 0xCA62C1D6u;
    }
    const std::uint32_t next = rotl(a, 5) + f + e + k + words[i];
    e = d;
    d = c;
    c = rotl(b, 30);
    b = a;
    a = next;
  }
  state_[0] += a;
  state_[1] += b;
  state_[2] += c;
  state_[3] += d;
  state_[4] += e;
}

void Sha1::take(std::uint8_t byte) noexcept {
  buffer_[buffered_++] = byte;
  ++length_;
  if (buffered_ == buffer_.size()) {
    block(buffer_.data());
    buffered_ = 0;
  }
}

void Sha1::update(std::span<const std::uint8_t> data) noexcept {
  for (const std::uint8_t byte : data) {
    take(byte);
  }
}

void Sha1::update(std::string_view text) noexcept {
  for (const char c : text) {
    take(static_cast<std::uint8_t>(c));
  }
}

std::array<std::uint8_t, 20> Sha1::finish() noexcept {
  const std::uint64_t bits = length_ * 8;
  take(0x80);
  while (buffered_ != 56) {
    take(0);
  }
  for (unsigned shift = 64; shift != 0; shift -= 8) {
    take(static_cast<std::uint8_t>(bits >> (shift - 8)));
  }
  std::array<std::uint8_t, 20> digest{};
  for (std::size_t i = 0; i < 5; ++i) {
    digest[4 * i] = static_cast<std::uint8_t>(state_[i] >> 24);
    digest[4 * i + 1] = static_cast<std::uint8_t>(state_[i] >> 16);
    digest[4 * i + 2] = static_cast<std::uint8_t>(state_[i] >> 8);
    digest[4 * i + 3] = static_cast<std::uint8_t>(state_[i]);
  }
  return digest;
}

std::size_t base64_encode(std::span<const std::uint8_t> in, std::span<char> out) noexcept {
  return encode(in, out, kBase64, true);
}

std::optional<std::size_t> base64_decode(std::string_view in, std::span<std::uint8_t> out) noexcept {
  return decode(in, out, false);
}

std::size_t base64url_encode(std::span<const std::uint8_t> in, std::span<char> out) noexcept {
  return encode(in, out, kBase64Url, false);
}

std::optional<std::size_t> base64url_decode(std::string_view in, std::span<std::uint8_t> out) noexcept {
  return decode(in, out, true);
}

// The ranges of RFC 3629 §4: the second byte after E0, ED, F0 and F4 is narrowed, which refuses overlong forms,
// surrogates and code points past U+10FFFF.
bool Utf8Stream::feed(std::uint8_t byte) noexcept {
  if (failed_) {
    return false;
  }
  if (need_ != 0) {
    if (byte < low_ || byte > high_) {
      failed_ = true;
      return false;
    }
    low_ = 0x80;
    high_ = 0xBF;
    --need_;
    return true;
  }
  if (byte < 0x80) {
    return true;
  }
  low_ = 0x80;
  high_ = 0xBF;
  if (byte >= 0xC2 && byte <= 0xDF) {
    need_ = 1;
  } else if (byte == 0xE0) {
    need_ = 2;
    low_ = 0xA0;
  } else if ((byte >= 0xE1 && byte <= 0xEC) || byte == 0xEE || byte == 0xEF) {
    need_ = 2;
  } else if (byte == 0xED) {
    need_ = 2;
    high_ = 0x9F;
  } else if (byte == 0xF0) {
    need_ = 3;
    low_ = 0x90;
  } else if (byte >= 0xF1 && byte <= 0xF3) {
    need_ = 3;
  } else if (byte == 0xF4) {
    need_ = 3;
    high_ = 0x8F;
  } else {
    failed_ = true;
    return false;
  }
  return true;
}

bool valid_utf8(std::span<const std::uint8_t> bytes) noexcept {
  Utf8Stream stream;
  for (const std::uint8_t byte : bytes) {
    if (!stream.feed(byte)) {
      return false;
    }
  }
  return stream.complete();
}

bool valid_utf8(std::string_view text) noexcept {
  Utf8Stream stream;
  for (const char c : text) {
    if (!stream.feed(static_cast<std::uint8_t>(c))) {
      return false;
    }
  }
  return stream.complete();
}

}  // namespace grace::serve
