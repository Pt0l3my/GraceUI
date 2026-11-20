// frame_fuzz.cpp: a libFuzzer target for the frame decoder and the session's state machine (§9.1, "Server code"; SRV
// §5.4). Any bytes are what a page sends after the 101, to a session that is open and to one that has sent its own
// Close. Each is fed whole, a byte at a time and at splits the bytes themselves choose, and every step (each message,
// each Pong, the Close and its code) must be the same across the three. In every run: no step takes more than it was
// given, need-more means everything was taken, a message stays within its cap, a text message is UTF-8, what the
// session writes fits its room, and the session always progresses or closes. The bytes also make a round trip as a
// payload: encoded by the server, masked as a page would, and decoded back. Any failure traps.
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

#include "grace/serve/codec.h"
#include "grace/serve/websocket.h"

namespace {

using namespace grace::serve;

constexpr std::size_t kMaxInput = 3 * kMaxMessage;

[[noreturn]] void fail() { __builtin_trap(); }

std::uint64_t next(std::uint64_t& state) {
  state += 0x9E3779B97F4A7C15u;
  std::uint64_t z = state;
  z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9u;
  z = (z ^ (z >> 27)) * 0x94D049BB133111EBu;
  return z ^ (z >> 31);
}

// FNV-1a over every step, so that three runs can be compared without keeping their steps.
class Digest {
 public:
  void add(std::uint8_t byte) noexcept {
    value_ ^= byte;
    value_ *= 0x100000001B3u;
  }
  void add(std::span<const std::uint8_t> bytes) noexcept {
    add(static_cast<std::uint8_t>(bytes.size()));
    add(static_cast<std::uint8_t>(bytes.size() >> 8));
    for (const std::uint8_t byte : bytes) {
      add(byte);
    }
  }
  std::uint64_t value() const noexcept { return value_; }

 private:
  std::uint64_t value_ = 0xCBF29CE484222325u;
};

std::uint64_t run(std::span<const std::uint8_t> bytes, std::uint64_t seed, bool closing) {
  Session session;
  std::array<std::uint8_t, kMaxControlFrame> out{};
  if (closing && session.start_close(1001, out) == 0) {
    fail();
  }
  Digest digest;
  std::size_t at = 0;
  std::size_t steps = 0;
  bool closed = false;
  while (at < bytes.size() && !closed) {
    std::size_t piece = bytes.size() - at;
    if (seed == 1) {
      piece = 1;
    } else if (seed > 1) {
      piece = 1 + static_cast<std::size_t>(next(seed) % 251);
    }
    piece = piece < bytes.size() - at ? piece : bytes.size() - at;
    std::span<const std::uint8_t> in = bytes.subspan(at, piece);
    at += piece;
    while (true) {
      if (++steps > 4 * bytes.size() + 8) {
        fail();  // neither progressing nor closing
      }
      std::size_t consumed = 0;
      std::size_t written = 0;
      const Session::Step step = session.on_input(in, consumed, out, written);
      if (consumed > in.size() || written > out.size()) {
        fail();
      }
      in = in.subspan(consumed);
      if (step == Session::Step::need_more) {
        if (!in.empty()) {
          fail();
        }
        break;
      }
      digest.add(static_cast<std::uint8_t>(step));
      if (step == Session::Step::message) {
        if (session.message().size() > kMaxMessage || (session.message_text() && !valid_utf8(session.message()))) {
          fail();
        }
        digest.add(static_cast<std::uint8_t>(session.message_text()));
        digest.add(session.message());
      } else {
        digest.add(std::span<const std::uint8_t>(out.data(), written));
      }
      if (step == Session::Step::close) {
        if (session.state() != Session::State::closed) {
          fail();
        }
        digest.add(static_cast<std::uint8_t>(session.close_code()));
        digest.add(static_cast<std::uint8_t>(session.close_code() >> 8));
        closed = true;
        break;
      }
    }
  }
  digest.add(static_cast<std::uint8_t>(session.state()));
  return digest.value();
}

// The server's frame of `payload`, masked as a page sends it, decoded back.
void round_trip(Opcode opcode, std::span<const std::uint8_t> payload) {
  static std::array<std::uint8_t, kMaxMessage + 16> server{};
  static std::array<std::uint8_t, kMaxMessage + 32> client{};
  const std::size_t size = encode_frame(opcode, payload, server);
  if (size == 0) {
    fail();
  }
  const std::size_t header = size - payload.size();
  for (std::size_t i = 0; i < header; ++i) {
    client[i] = server[i];
  }
  client[1] = static_cast<std::uint8_t>(client[1] | 0x80);
  const std::array<std::uint8_t, 4> mask = {0x5C, 0xA3, 0x00, 0xFF};
  for (std::size_t i = 0; i < 4; ++i) {
    client[header + i] = mask[i];
  }
  for (std::size_t i = 0; i < payload.size(); ++i) {
    client[header + 4 + i] = static_cast<std::uint8_t>(server[header + i] ^ mask[i % 4]);
  }
  FrameDecoder decoder;
  std::size_t consumed = 0;
  const FrameDecoder::Event event = decoder.feed(std::span<const std::uint8_t>(client.data(), size + 4), consumed);
  const FrameDecoder::Event expected = opcode == Opcode::text ? FrameDecoder::Event::text : FrameDecoder::Event::binary;
  if (event != expected || consumed != size + 4 || decoder.payload().size() != payload.size()) {
    fail();
  }
  for (std::size_t i = 0; i < payload.size(); ++i) {
    if (decoder.payload()[i] != payload[i]) {
      fail();
    }
  }
}

}  // namespace

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
  if (size > kMaxInput) {
    return 0;
  }
  const std::span<const std::uint8_t> bytes(data, size);
  std::uint64_t seed = 2;
  for (std::size_t i = 0; i < size; ++i) {
    seed = seed * 131 + data[i];
  }
  seed |= 2;
  for (const bool closing : {false, true}) {
    const std::uint64_t whole = run(bytes, 0, closing);
    if (run(bytes, 1, closing) != whole || run(bytes, seed, closing) != whole) {
      fail();
    }
  }
  const std::span<const std::uint8_t> payload = bytes.first(size < kMaxMessage ? size : kMaxMessage);
  round_trip(Opcode::binary, payload);
  if (valid_utf8(payload)) {
    round_trip(Opcode::text, payload);
  }
  return 0;
}
