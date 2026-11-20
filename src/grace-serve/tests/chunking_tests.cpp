// The chunking differential (SRV §5.4; §9.1, "Server code"): every head and every frame stream here is fed whole, a
// byte at a time and in random splits, and the outcomes must be identical, down to the bytes taken and every step.
#include <doctest/doctest.h>

#include <array>
#include <random>
#include <string>
#include <string_view>
#include <vector>

#include "grace/serve/head.h"
#include "grace/serve/websocket.h"
#include "support.h"

using namespace grace::serve;
using namespace grace::serve::test;

namespace {

struct HeadOutcome {
  HeadParser::State state = HeadParser::State::need_more;
  Refusal refusal = Refusal::none;
  std::size_t consumed = 0;
  Method method = Method::get;
  std::string target;
  HostMatch host = HostMatch::none;
  std::uint64_t content_length = 0;
  std::size_t fields = 0;
  std::size_t size = 0;
  bool operator==(const HeadOutcome&) const = default;
};

HeadOutcome run_head(std::string_view bytes, const std::vector<std::size_t>& sizes, const HeadRules& rules) {
  HeadParser parser(rules);
  HeadOutcome outcome;
  std::size_t at = 0;
  for (const std::size_t piece : pieces(bytes.size(), sizes)) {
    const HeadParser::Fed fed = parser.feed(bytes.substr(at, piece));
    outcome.consumed += fed.consumed;
    at += piece;
    if (fed.state != HeadParser::State::need_more) {
      break;
    }
  }
  outcome.state = parser.state();
  outcome.refusal = parser.refusal();
  if (outcome.state == HeadParser::State::complete) {
    const RequestHead& head = parser.head();
    outcome.method = head.method;
    outcome.target = std::string(head.target);
    outcome.host = head.host;
    outcome.content_length = head.content_length;
    outcome.fields = head.fields.size();
    outcome.size = head.size;
  }
  return outcome;
}

std::vector<std::string> heads() {
  std::vector<std::string> all = {
      get_head(),
      get_head("/index.html", "Sec-Fetch-Mode: navigate\r\nSec-Fetch-Dest: document\r\nCookie: a=1; en_session=x\r\n"),
      get_head("/app.js?v=1"),
      "POST /api/ticket HTTP/1.1\r\nHost: 127.0.0.1:8642\r\nContent-Length: 2\r\nOrigin: http://127.0.0.1:8642\r\n"
      "\r\n{}",
      get_head("/ws", "Upgrade: websocket\r\nConnection: keep-alive, Upgrade\r\nSec-WebSocket-Version: 13\r\n"
                      "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n") + "\x81\x85",
      "GET / HTTP/1.1\r\nHost: localhost:8642\r\n\r\n",
      "GET / HTTP/1.1\r\nHost: 192.0.2.7:8642\r\n\r\n",
      "PUT / HTTP/1.1\r\nHost: 127.0.0.1:8642\r\n\r\n",
      "GET / HTTP/1.0\r\nHost: 127.0.0.1:8642\r\n\r\n",
      "GET http://x/ HTTP/1.1\r\nHost: 127.0.0.1:8642\r\n\r\n",
      "GET / HTTP/1.1\nHost: 127.0.0.1:8642\n\n",
      "GET / HTTP/1.1\r\nHost: 127.0.0.1:8642\r\r\n\r\n",
      "\r\nGET / HTTP/1.1\r\nHost: 127.0.0.1:8642\r\n\r\n",
      std::string("GET / HTTP/1.1\r\nHost: 127.0.0.1:8642\r\nX: \0\r\n\r\n", 46),
      get_head("/", "X: a\r\n b\r\n"),
      get_head("/", "X : a\r\n"),
      get_head("/", "Host: 127.0.0.1:8642\r\n"),
      "GET / HTTP/1.1\r\n\r\n",
      "GET / HTTP/1.1\r\nHost: attacker.example\r\n\r\n",
      "POST / HTTP/1.1\r\nHost: 127.0.0.1:8642\r\nTransfer-Encoding: chunked\r\n\r\n0\r\n\r\n",
      "POST / HTTP/1.1\r\nHost: 127.0.0.1:8642\r\nContent-Length: 1\r\nContent-Length: 1\r\n\r\nx",
      "POST / HTTP/1.1\r\nHost: 127.0.0.1:8642\r\nContent-Length: 99999999999\r\n\r\n",
      "POST / HTTP/1.1\r\nHost: 127.0.0.1:8642\r\nContent-Length: 1\r\nExpect: 100-continue\r\n\r\n",
      "POST / HTTP/1.1\r\nHost: 127.0.0.1:8642\r\n\r\n",
      get_head("/", "Content-Length: 3\r\n") + "abc",
      get_head("/" + std::string(kMaxTarget, 'a')),
      "GET /" + std::string(kMaxHead + 10, 'a'),
      get_head("/", "X-Pad: " + std::string(kMaxHead, 'p') + "\r\n"),
      get_head("/", "Cookie: " + std::string(kMaxCookie + 1, 'c') + "\r\n"),
      "GET / HTTP/1.1\r\nHost: 127.0.0.1:8642\r\n",  // never ends: need more
      "GET / HTTP/1.1\r",
      "",
  };
  std::string many;
  for (std::size_t i = 0; i <= kMaxFields; ++i) {
    many += "X-" + std::to_string(i) + ": v\r\n";
  }
  all.push_back(get_head("/", many));
  return all;
}

struct Trace {
  std::vector<std::string> steps;
  Session::State state = Session::State::open;
  bool operator==(const Trace&) const = default;
};

Trace run_session(const std::vector<std::uint8_t>& stream, const std::vector<std::size_t>& sizes, bool closing) {
  Session session;
  std::array<std::uint8_t, kMaxControlFrame> out{};
  if (closing) {
    session.start_close(1000, out);
  }
  Trace trace;
  std::size_t at = 0;
  bool closed = false;
  for (const std::size_t piece : pieces(stream.size(), sizes)) {
    std::span<const std::uint8_t> in(stream.data() + at, piece);
    at += piece;
    while (!closed) {
      std::size_t consumed = 0;
      std::size_t written = 0;
      const Session::Step step = session.on_input(in, consumed, out, written);
      REQUIRE(consumed <= in.size());
      in = in.subspan(consumed);
      if (step == Session::Step::need_more) {
        REQUIRE(in.empty());
        break;
      }
      std::string line;
      switch (step) {
        case Session::Step::message:
          line = (session.message_text() ? "text:" : "binary:") + text_of(session.message());
          break;
        case Session::Step::send:
          line = "send:" + text_of(std::span<const std::uint8_t>(out.data(), written));
          break;
        case Session::Step::close:
          line = "close:" + std::to_string(session.close_code()) + ":" +
                 text_of(std::span<const std::uint8_t>(out.data(), written));
          closed = true;
          break;
        case Session::Step::need_more:
          break;
      }
      trace.steps.push_back(line);
    }
    if (closed) {
      break;
    }
  }
  trace.state = session.state();
  return trace;
}

std::vector<std::uint8_t> close_with(std::uint16_t code, std::string_view reason = "") {
  FrameSpec spec;
  spec.opcode = 8;
  spec.payload = {static_cast<std::uint8_t>(code >> 8), static_cast<std::uint8_t>(code)};
  for (const char c : reason) {
    spec.payload.push_back(static_cast<std::uint8_t>(c));
  }
  return frame(spec);
}

std::vector<std::vector<std::uint8_t>> streams() {
  FrameSpec ping;
  ping.opcode = 9;
  ping.payload = bytes_of("are you there");
  FrameSpec pong;
  pong.opcode = 10;
  FrameSpec unmasked;
  unmasked.masked = false;
  unmasked.payload = bytes_of("x");
  FrameSpec reserved;
  reserved.rsv = 0x40;
  FrameSpec big;
  big.opcode = 2;
  big.payload = std::vector<std::uint8_t>(kMaxMessage, 0x5A);
  FrameSpec too_big;
  too_big.claimed = kMaxMessage + 1;
  FrameSpec sixteen;
  sixteen.payload = std::vector<std::uint8_t>(300, 'm');
  FrameSpec padded;
  padded.payload = bytes_of("x");
  padded.form = LengthForm::sixteen;
  return {
      {0x81, 0x85, 0x37, 0xFA, 0x21, 0x3D, 0x7F, 0x9F, 0x4D, 0x51, 0x58},
      join({text_frame("Hel", false), frame(ping), text_frame("lo", true, 0), text_frame("again")}),
      join({text_frame("caf\xC3", false), frame(pong), text_frame("\xA9 ok", true, 0), close_with(1000, "bye")}),
      join({text_frame("one"), text_frame("two"), frame(big), frame(sixteen), close_with(1001)}),
      join({text_frame(""), text_frame("", false), text_frame("", true, 0), frame(ping)}),
      join({text_frame("a"), frame(unmasked), text_frame("never")}),
      join({text_frame("a"), frame(reserved)}),
      join({text_frame("a"), frame(too_big), text_frame("never")}),
      join({text_frame("ab\xC0\xAF tail")}),
      join({text_frame("caf\xC3")}),
      join({text_frame("Hel", false), text_frame("new")}),
      join({text_frame("lo", true, 0)}),
      join({frame(padded)}),
      join({close_with(1005)}),
      join({close_with(1000, "\xFF")}),
      join({text_frame("half"), std::vector<std::uint8_t>{0x81, 0xFE, 0x01}}),  // cut inside a header
  };
}

}  // namespace

TEST_CASE("every head gives the same outcome however it is split") {
  const HeadRules loopback = loopback_rules();
  const HeadRules network = network_rules();
  std::mt19937 random(20261120);
  for (const std::string& head : heads()) {
    CAPTURE(head.substr(0, 60));
    for (const HeadRules* rules : {&loopback, &network}) {
      const HeadOutcome whole = run_head(head, {}, *rules);
      CHECK(run_head(head, {1}, *rules) == whole);
      for (int round = 0; round < 12; ++round) {
        std::vector<std::size_t> sizes;
        for (int i = 0; i < 16; ++i) {
          sizes.push_back(1 + random() % 23);
        }
        CHECK(run_head(head, sizes, *rules) == whole);
      }
    }
  }
}

TEST_CASE("every frame stream gives the same steps however it is split, open or closing") {
  std::mt19937 random(6455);
  for (const std::vector<std::uint8_t>& stream : streams()) {
    CAPTURE(stream.size());
    for (const bool closing : {false, true}) {
      const Trace whole = run_session(stream, {}, closing);
      CHECK(run_session(stream, {1}, closing) == whole);
      for (int round = 0; round < 12; ++round) {
        std::vector<std::size_t> sizes;
        for (int i = 0; i < 16; ++i) {
          sizes.push_back(1 + random() % 37);
        }
        CHECK(run_session(stream, sizes, closing) == whole);
      }
    }
  }
}
