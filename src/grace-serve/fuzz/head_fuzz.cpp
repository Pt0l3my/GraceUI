// head_fuzz.cpp: a libFuzzer target for the request-head parser and the checks that read a head (§9.1, "Server code";
// SRV §5.4). Any bytes are a connection's first bytes, on the loopback listener and on a network one. Each is fed whole,
// a byte at a time and at splits the bytes themselves choose, and the three outcomes must be the same (the chunking
// differential); a complete head then goes through every check and the upgrade, and every answer the writer makes
// must be whole, within its room and made of CRLF-ended lines of printable ASCII. The raw bytes also go straight to
// the Host, cookie, token and Base64 readers, and to the redirect's guard as a listed name. Any failure traps.
#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

#include "grace/serve/checks.h"
#include "grace/serve/codec.h"
#include "grace/serve/head.h"
#include "grace/serve/response.h"
#include "grace/serve/websocket.h"

namespace {

using namespace grace::serve;

constexpr std::size_t kMaxInput = 2 * kMaxHead + 256;
constexpr std::string_view kAddresses[] = {"192.0.2.7", "[2001:db8::7]"};  // invented (RFC 5737, RFC 3849)
constexpr std::string_view kSession = "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA";  // a token of 32 zero bytes

struct Outcome {
  HeadParser::State state = HeadParser::State::need_more;
  Refusal refusal = Refusal::none;
  std::size_t consumed = 0;
  Method method = Method::get;
  HostMatch host = HostMatch::none;
  bool has_query = false;
  std::uint64_t content_length = 0;
  std::size_t fields = 0;
  std::size_t size = 0;
  std::size_t target = 0;
  bool operator==(const Outcome&) const = default;
};

[[noreturn]] void fail() { __builtin_trap(); }

std::uint64_t next(std::uint64_t& state) {
  state += 0x9E3779B97F4A7C15u;
  std::uint64_t z = state;
  z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9u;
  z = (z ^ (z >> 27)) * 0x94D049BB133111EBu;
  return z ^ (z >> 31);
}

// Pieces of 0 (the whole), 1 (a byte at a time) or sizes from `seed`.
Outcome run(std::string_view bytes, std::uint64_t seed, HeadParser& parser) {
  parser.reset();
  Outcome outcome;
  std::size_t at = 0;
  while (at < bytes.size()) {
    std::size_t piece = bytes.size() - at;
    if (seed == 1) {
      piece = 1;
    } else if (seed > 1) {
      piece = 1 + static_cast<std::size_t>(next(seed) % 97);
    }
    piece = piece < bytes.size() - at ? piece : bytes.size() - at;
    const HeadParser::Fed fed = parser.feed(bytes.substr(at, piece));
    if (fed.consumed > piece) {
      fail();
    }
    outcome.consumed += fed.consumed;
    at += piece;
    if (fed.state != HeadParser::State::need_more) {
      break;
    }
    if (fed.consumed != piece) {
      fail();  // need more, but bytes were left untaken
    }
  }
  outcome.state = parser.state();
  outcome.refusal = parser.refusal();
  if (outcome.state == HeadParser::State::complete) {
    const RequestHead& head = parser.head();
    outcome.method = head.method;
    outcome.host = head.host;
    outcome.has_query = head.has_query;
    outcome.content_length = head.content_length;
    outcome.fields = head.fields.size();
    outcome.size = head.size;
    outcome.target = head.target.size();
    if (head.size != outcome.consumed || head.size > kMaxHead || head.target.size() > kMaxTarget ||
        head.fields.size() > kMaxFields || head.host == HostMatch::none) {
      fail();
    }
  } else if (outcome.state == HeadParser::State::refused) {
    if (status_of(outcome.refusal) < 400 || outcome.consumed > kMaxHead) {
      fail();
    }
  } else if (outcome.consumed != bytes.size() || outcome.consumed > kMaxHead) {
    fail();
  }
  return outcome;
}

// A head the writer made: CRLF-ended lines of printable ASCII, ending in an empty line.
void check_written(std::span<const char> out, std::size_t size) {
  if (size == 0 || size > out.size() || size < 4) {
    fail();
  }
  for (std::size_t i = 0; i < size; ++i) {
    const auto byte = static_cast<unsigned char>(out[i]);
    if (byte == '\r') {
      if (i + 1 >= size || out[i + 1] != '\n') {
        fail();
      }
    } else if (byte == '\n') {
      if (i == 0 || out[i - 1] != '\r') {
        fail();
      }
    } else if (byte < 0x20 || byte > 0x7E) {
      fail();
    }
  }
  if (std::string_view(out.data() + size - 4, 4) != "\r\n\r\n") {
    fail();
  }
}

void answer(Refusal refusal) {
  std::array<char, kMaxResponseHead> out{};
  Response response;
  response.status = status_of(refusal);
  response.content_type = "text/plain; charset=utf-8";
  response.content_length = refusal_body(refusal).size();
  check_written(out, write_response_head(response, out));
}

void exercise(const RequestHead& head, const HeadRules& rules, std::size_t after) {
  check_origin(head, rules.listener);
  fetch_site_allowed(head);
  is_page_load(head);
  content_type_is_json(head);
  check_session_cookie(head.cookie, kSession);
  const Refusal api = check_api_request(head, rules.listener, kSessionCookie, kSession);
  if (api != Refusal::none) {
    answer(api);
  }
  std::array<char, kAcceptChars> accept{};
  const Refusal upgrade = check_upgrade(head, after, accept);
  std::array<char, kMaxResponseHead> out{};
  if (upgrade == Refusal::none) {
    check_written(out, write_switching_protocols(std::string_view(accept.data(), accept.size()), out));
  } else {
    answer(upgrade);
  }
  for (std::size_t i = 0; i < head.fields.size(); ++i) {
    head.first(head.fields[i].name);
    head.has_token(head.fields[i].name, "upgrade");
  }
}

}  // namespace

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
  if (size > kMaxInput) {
    return 0;
  }
  // The bytes as chars, by value: the pure module casts no pointer (D110).
  static std::array<char, kMaxInput> text{};
  for (std::size_t i = 0; i < size; ++i) {
    text[i] = static_cast<char>(data[i]);
  }
  const std::string_view bytes(text.data(), size);

  HeadRules loopback;
  HeadRules network;
  network.listener.network = true;
  network.listener.addresses = kAddresses;
  network.max_content_length = 4096;
  std::uint64_t seed = 2;
  for (std::size_t i = 0; i < size; ++i) {
    seed = seed * 131 + data[i];
  }
  seed |= 2;

  for (const HeadRules* rules : {&loopback, &network}) {
    HeadParser whole(*rules);
    HeadParser bytewise(*rules);
    HeadParser split(*rules);
    const Outcome a = run(bytes, 0, whole);
    const Outcome b = run(bytes, 1, bytewise);
    const Outcome c = run(bytes, seed, split);
    if (!(a == b) || !(a == c)) {
      fail();
    }
    if (a.state == HeadParser::State::complete) {
      exercise(whole.head(), *rules, size - a.consumed);
    } else if (a.state == HeadParser::State::refused) {
      answer(a.refusal);
    }
  }

  // The readers on raw bytes.
  check_host(bytes, loopback.listener);
  check_host(bytes, network.listener);
  check_session_cookie(bytes, kSession);
  check_cookie(bytes, "x", bytes);
  valid_token(bytes);
  valid_utf8(bytes);
  static std::array<std::uint8_t, kMaxInput> decoded{};
  static std::array<char, base64_size(kMaxInput)> again{};
  if (const auto n = base64_decode(bytes, decoded)) {
    if (base64_encode(std::span<const std::uint8_t>(decoded.data(), *n), again) != size) {
      fail();  // strict: one encoding per value, so it round-trips exactly
    }
    if (std::string_view(again.data(), size) != bytes) {
      fail();
    }
  }
  if (const auto n = base64url_decode(bytes, decoded)) {
    if (base64url_encode(std::span<const std::uint8_t>(decoded.data(), *n), again) != size ||
        std::string_view(again.data(), size) != bytes) {
      fail();
    }
  }
  std::array<char, kMaxResponseHead + kMaxTarget> redirect{};
  check_written(redirect, write_redirect(8642, bytes, redirect));
  return 0;
}
