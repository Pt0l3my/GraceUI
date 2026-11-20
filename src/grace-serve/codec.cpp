// codec.cpp: SHA-1 (FIPS 180-4 §6.1), SHA-256 (§6.2), Base64 and base64url (RFC 4648 §4, §5) and UTF-8 (RFC 3629 §4),
// our own.
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

namespace {

constexpr std::uint32_t rotr(std::uint32_t value, unsigned bits) noexcept {
  return (value >> bits) | (value << (32 - bits));
}

// FIPS 180-4 §4.2.2.
constexpr std::array<std::uint32_t, 64> kSha256Rounds = {
    0x428A2F98u, 0x71374491u, 0xB5C0FBCFu, 0xE9B5DBA5u, 0x3956C25Bu, 0x59F111F1u, 0x923F82A4u, 0xAB1C5ED5u,
    0xD807AA98u, 0x12835B01u, 0x243185BEu, 0x550C7DC3u, 0x72BE5D74u, 0x80DEB1FEu, 0x9BDC06A7u, 0xC19BF174u,
    0xE49B69C1u, 0xEFBE4786u, 0x0FC19DC6u, 0x240CA1CCu, 0x2DE92C6Fu, 0x4A7484AAu, 0x5CB0A9DCu, 0x76F988DAu,
    0x983E5152u, 0xA831C66Du, 0xB00327C8u, 0xBF597FC7u, 0xC6E00BF3u, 0xD5A79147u, 0x06CA6351u, 0x14292967u,
    0x27B70A85u, 0x2E1B2138u, 0x4D2C6DFCu, 0x53380D13u, 0x650A7354u, 0x766A0ABBu, 0x81C2C92Eu, 0x92722C85u,
    0xA2BFE8A1u, 0xA81A664Bu, 0xC24B8B70u, 0xC76C51A3u, 0xD192E819u, 0xD6990624u, 0xF40E3585u, 0x106AA070u,
    0x19A4C116u, 0x1E376C08u, 0x2748774Cu, 0x34B0BCB5u, 0x391C0CB3u, 0x4ED8AA4Au, 0x5B9CCA4Fu, 0x682E6FF3u,
    0x748F82EEu, 0x78A5636Fu, 0x84C87814u, 0x8CC70208u, 0x90BEFFFAu, 0xA4506CEBu, 0xBEF9A3F7u, 0xC67178F2u};

}  // namespace

void Sha256::block(const std::uint8_t* data) noexcept {
  std::array<std::uint32_t, 64> words{};
  for (std::size_t i = 0; i < 16; ++i) {
    words[i] = (std::uint32_t{data[4 * i]} << 24) | (std::uint32_t{data[4 * i + 1]} << 16) |
               (std::uint32_t{data[4 * i + 2]} << 8) | std::uint32_t{data[4 * i + 3]};
  }
  for (std::size_t i = 16; i < 64; ++i) {
    const std::uint32_t s0 = rotr(words[i - 15], 7) ^ rotr(words[i - 15], 18) ^ (words[i - 15] >> 3);
    const std::uint32_t s1 = rotr(words[i - 2], 17) ^ rotr(words[i - 2], 19) ^ (words[i - 2] >> 10);
    words[i] = words[i - 16] + s0 + words[i - 7] + s1;
  }
  std::array<std::uint32_t, 8> v = state_;
  for (std::size_t i = 0; i < 64; ++i) {
    const std::uint32_t s1 = rotr(v[4], 6) ^ rotr(v[4], 11) ^ rotr(v[4], 25);
    const std::uint32_t choose = (v[4] & v[5]) ^ (~v[4] & v[6]);
    const std::uint32_t t1 = v[7] + s1 + choose + kSha256Rounds[i] + words[i];
    const std::uint32_t s0 = rotr(v[0], 2) ^ rotr(v[0], 13) ^ rotr(v[0], 22);
    const std::uint32_t majority = (v[0] & v[1]) ^ (v[0] & v[2]) ^ (v[1] & v[2]);
    const std::uint32_t t2 = s0 + majority;
    v[7] = v[6];
    v[6] = v[5];
    v[5] = v[4];
    v[4] = v[3] + t1;
    v[3] = v[2];
    v[2] = v[1];
    v[1] = v[0];
    v[0] = t1 + t2;
  }
  for (std::size_t i = 0; i < 8; ++i) {
    state_[i] += v[i];
  }
}

void Sha256::take(std::uint8_t byte) noexcept {
  buffer_[buffered_++] = byte;
  ++length_;
  if (buffered_ == buffer_.size()) {
    block(buffer_.data());
    buffered_ = 0;
  }
}

void Sha256::update(std::span<const std::uint8_t> data) noexcept {
  std::size_t at = 0;
  while (at < data.size() && buffered_ != 0) {
    take(data[at++]);
  }
  // Whole blocks straight from the input: the web files are hashed at every start.
  for (; data.size() - at >= buffer_.size(); at += buffer_.size()) {
    block(data.data() + at);
    length_ += buffer_.size();
  }
  for (; at < data.size(); ++at) {
    take(data[at]);
  }
}

void Sha256::update(std::string_view text) noexcept {
  for (const char c : text) {
    take(static_cast<std::uint8_t>(c));
  }
}

std::array<std::uint8_t, 32> Sha256::finish() noexcept {
  const std::uint64_t bits = length_ * 8;
  take(0x80);
  while (buffered_ != 56) {
    take(0);
  }
  for (unsigned shift = 64; shift != 0; shift -= 8) {
    take(static_cast<std::uint8_t>(bits >> (shift - 8)));
  }
  std::array<std::uint8_t, 32> digest{};
  for (std::size_t i = 0; i < 8; ++i) {
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
