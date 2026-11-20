// The frame codec and the session (websocket.h): RFC 6455 §5.7's examples, every failure with its Close code, the
// message cap seen from a header before its payload, UTF-8 across fragments, control frames between them, Close handled
// once, the encoder's minimal lengths, round trips, and no allocation while decoding.
#include <doctest/doctest.h>

#include <array>
#include <string>
#include <string_view>
#include <vector>

#include "alloc_counter.h"
#include "grace/serve/websocket.h"
#include "support.h"

using namespace grace::serve;
using namespace grace::serve::test;
using Event = FrameDecoder::Event;
using Step = Session::Step;

namespace {

struct Decoded {
  Event event = Event::need_more;
  std::string payload;
  std::uint16_t code = 0;
  FrameFailure failure = FrameFailure::none;
  std::size_t consumed = 0;
};

// The first event `bytes` give, fed whole.
Decoded first_event(const std::vector<std::uint8_t>& bytes) {
  FrameDecoder decoder;
  Decoded out;
  out.event = decoder.feed(bytes, out.consumed);
  out.payload = text_of(decoder.payload());
  out.code = decoder.close_code();
  out.failure = decoder.failure();
  return out;
}

// Every event of a stream, fed whole, then nothing more once finished.
std::vector<Decoded> events_of(const std::vector<std::uint8_t>& bytes) {
  FrameDecoder decoder;
  std::vector<Decoded> events;
  std::size_t at = 0;
  while (at < bytes.size()) {
    Decoded out;
    out.event = decoder.feed(std::span<const std::uint8_t>(bytes).subspan(at), out.consumed);
    at += out.consumed;
    if (out.event == Event::need_more) {
      break;
    }
    out.payload = text_of(decoder.payload());
    out.code = decoder.close_code();
    out.failure = decoder.failure();
    events.push_back(out);
  }
  return events;
}

FrameFailure failure_of(const std::vector<std::uint8_t>& bytes) {
  const std::vector<Decoded> events = events_of(bytes);
  return events.empty() || events.back().event != Event::failed ? FrameFailure::none : events.back().failure;
}

std::vector<std::uint8_t> control(std::uint8_t opcode, std::vector<std::uint8_t> payload, bool fin = true) {
  FrameSpec spec;
  spec.opcode = opcode;
  spec.payload = std::move(payload);
  spec.fin = fin;
  return frame(spec);
}

std::vector<std::uint8_t> close_frame(std::uint16_t code, std::string_view reason = "") {
  std::vector<std::uint8_t> payload = {static_cast<std::uint8_t>(code >> 8), static_cast<std::uint8_t>(code)};
  for (const char c : reason) {
    payload.push_back(static_cast<std::uint8_t>(c));
  }
  return control(8, payload);
}

// Masks a frame the server's encoder wrote, as a page would send it, for a round trip.
std::vector<std::uint8_t> as_client(std::span<const std::uint8_t> server_frame) {
  std::size_t header = 2;
  const std::uint8_t length = server_frame[1] & 0x7F;
  header += length == 126 ? 2 : length == 127 ? 8 : 0;
  std::vector<std::uint8_t> out(server_frame.begin(), server_frame.begin() + static_cast<std::ptrdiff_t>(header));
  out[1] |= 0x80;
  const std::array<std::uint8_t, 4> mask = {0x01, 0x80, 0x7F, 0xFE};
  out.insert(out.end(), mask.begin(), mask.end());
  for (std::size_t i = header; i < server_frame.size(); ++i) {
    out.push_back(static_cast<std::uint8_t>(server_frame[i] ^ mask[(i - header) % 4]));
  }
  return out;
}

}  // namespace

TEST_CASE("RFC 6455 §5.7's examples") {
  // A single-frame masked text message, "Hello".
  const std::vector<std::uint8_t> hello = {0x81, 0x85, 0x37, 0xFA, 0x21, 0x3D, 0x7F, 0x9F, 0x4D, 0x51, 0x58};
  const Decoded text = first_event(hello);
  CHECK(text.event == Event::text);
  CHECK(text.payload == "Hello");
  CHECK(text.consumed == hello.size());
  // A masked Pong, "Hello".
  const std::vector<std::uint8_t> pong = {0x8A, 0x85, 0x37, 0xFA, 0x21, 0x3D, 0x7F, 0x9F, 0x4D, 0x51, 0x58};
  CHECK(first_event(pong).event == Event::pong);
  CHECK(first_event(pong).payload == "Hello");
  // The unmasked forms are the server's: the encoder writes them, and the decoder refuses them from a page.
  const std::vector<std::uint8_t> unmasked = {0x81, 0x05, 0x48, 0x65, 0x6C, 0x6C, 0x6F};
  std::array<std::uint8_t, 16> out{};
  const std::size_t size = encode_frame(Opcode::text, bytes_of("Hello"), out);
  CHECK(std::vector<std::uint8_t>(out.begin(), out.begin() + static_cast<std::ptrdiff_t>(size)) == unmasked);
  CHECK(failure_of(unmasked) == FrameFailure::unmasked);
  const std::vector<std::uint8_t> ping = {0x89, 0x05, 0x48, 0x65, 0x6C, 0x6C, 0x6F};
  CHECK(encode_frame(Opcode::ping, bytes_of("Hello"), out) == ping.size());
  CHECK(std::vector<std::uint8_t>(out.begin(), out.begin() + 7) == ping);
  // A fragmented text message, "Hel" then "lo", masked as a page sends it.
  const std::vector<Decoded> fragmented = events_of(join({text_frame("Hel", false), text_frame("lo", true, 0)}));
  REQUIRE(fragmented.size() == 1);
  CHECK(fragmented[0].event == Event::text);
  CHECK(fragmented[0].payload == "Hello");
  // 256 bytes and 64 KiB of binary: the 16-bit and 64-bit lengths' headers.
  std::array<std::uint8_t, 10> header{};
  CHECK(encode_frame_header(Opcode::binary, 256, header) == 4);
  CHECK(std::vector<std::uint8_t>(header.begin(), header.begin() + 4) == std::vector<std::uint8_t>{0x82, 0x7E, 0x01,
                                                                                                    0x00});
  CHECK(encode_frame_header(Opcode::binary, 65536, header) == 10);
  CHECK(std::vector<std::uint8_t>(header.begin(), header.end()) ==
        std::vector<std::uint8_t>{0x82, 0x7F, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00});
}

TEST_CASE("a page's frame header: masked, no reserved bit, a known opcode, minimal lengths") {
  FrameSpec spec;
  spec.payload = bytes_of("x");
  spec.masked = false;
  CHECK(failure_of(frame(spec)) == FrameFailure::unmasked);
  for (const std::uint8_t rsv : {0x40, 0x20, 0x10}) {
    spec = FrameSpec{};
    spec.payload = bytes_of("x");
    spec.rsv = rsv;
    CAPTURE(rsv);
    CHECK(failure_of(frame(spec)) == FrameFailure::reserved_bits);
  }
  for (const std::uint8_t opcode : {3, 4, 5, 6, 7, 11, 12, 13, 14, 15}) {
    spec = FrameSpec{};
    spec.opcode = opcode;
    CAPTURE(opcode);
    CHECK(failure_of(frame(spec)) == FrameFailure::reserved_opcode);
  }
  spec = FrameSpec{};
  spec.payload = bytes_of("short");
  spec.form = LengthForm::sixteen;
  CHECK(failure_of(frame(spec)) == FrameFailure::non_minimal_length);
  spec.payload = std::vector<std::uint8_t>(125, 'a');
  CHECK(failure_of(frame(spec)) == FrameFailure::non_minimal_length);
  spec.payload = std::vector<std::uint8_t>(126, 'a');
  CHECK(failure_of(frame(spec)) == FrameFailure::none);  // 126 is the first length the 16-bit form may carry
  spec.payload = std::vector<std::uint8_t>(300, 'a');
  spec.form = LengthForm::sixty_four;
  CHECK(failure_of(frame(spec)) == FrameFailure::non_minimal_length);
  spec.payload.clear();
  spec.claimed = 0xFFFF;
  CHECK(failure_of(frame(spec)) == FrameFailure::non_minimal_length);
  spec.claimed = std::uint64_t{1} << 63;
  CHECK(failure_of(frame(spec)) == FrameFailure::length_high_bit);
  spec.claimed = UINT64_MAX - 1;
  CHECK(failure_of(frame(spec)) == FrameFailure::length_high_bit);
  CHECK(failure_code(FrameFailure::unmasked) == 1002);
  CHECK(failure_code(FrameFailure::reserved_bits) == 1002);
  CHECK(failure_code(FrameFailure::non_minimal_length) == 1002);
}

TEST_CASE("the message cap is checked from the header, before any payload") {
  FrameSpec spec;
  spec.opcode = 2;
  spec.payload = std::vector<std::uint8_t>(kMaxMessage, 0xAB);
  const std::vector<Decoded> whole = events_of(frame(spec));
  REQUIRE(whole.size() == 1);
  CHECK(whole[0].event == Event::binary);
  CHECK(whole[0].payload.size() == kMaxMessage);

  // One byte more fails at its header: only the header and nothing of the payload is taken.
  spec.payload.clear();
  spec.claimed = kMaxMessage + 1;
  const std::vector<std::uint8_t> header_only = frame(spec);
  const Decoded big = first_event(header_only);
  CHECK(big.event == Event::failed);
  CHECK(big.failure == FrameFailure::too_big);
  CHECK(big.code == 1009);
  CHECK(big.consumed == 4);  // two bytes and the 16-bit length: the mask is never read
  spec.claimed = std::uint64_t{1} << 40;
  CHECK(failure_of(frame(spec)) == FrameFailure::too_big);

  // The cap is the reassembled message's, not a frame's (Crow's 2026 advisory).
  const std::vector<std::uint8_t> half(kMaxMessage / 2, 'a');
  FrameSpec first;
  first.fin = false;
  first.payload = half;
  FrameSpec second;
  second.opcode = 0;
  second.fin = false;
  second.payload = half;
  FrameSpec third;
  third.opcode = 0;
  third.payload = bytes_of("a");
  const std::vector<Decoded> over = events_of(join({frame(first), frame(second), frame(third)}));
  REQUIRE(over.size() == 1);
  CHECK(over[0].failure == FrameFailure::too_big);
  second.fin = true;
  const std::vector<Decoded> exact = events_of(join({frame(first), frame(second)}));
  REQUIRE(exact.size() == 1);
  CHECK(exact[0].event == Event::text);
  CHECK(exact[0].payload.size() == kMaxMessage);
}

TEST_CASE("fragments: continuations only inside a message, and control frames between them") {
  CHECK(failure_of(text_frame("lo", true, 0)) == FrameFailure::stray_continuation);
  CHECK(failure_of(join({text_frame("a"), text_frame("b", true, 0)})) == FrameFailure::stray_continuation);
  CHECK(failure_of(join({text_frame("Hel", false), text_frame("lo")})) == FrameFailure::interleaved_message);
  FrameSpec binary;
  binary.opcode = 2;
  binary.payload = bytes_of("b");
  CHECK(failure_of(join({text_frame("Hel", false), frame(binary)})) == FrameFailure::interleaved_message);
  CHECK(failure_code(FrameFailure::stray_continuation) == 1002);

  // A Ping and a Pong between fragments are answered and the message goes on.
  const std::vector<Decoded> events = events_of(join({text_frame("He", false), control(9, bytes_of("p1")),
                                                     text_frame("l", false, 0), control(10, {}),
                                                     text_frame("lo", true, 0), text_frame("next")}));
  REQUIRE(events.size() == 4);
  CHECK(events[0].event == Event::ping);
  CHECK(events[0].payload == "p1");
  CHECK(events[1].event == Event::pong);
  CHECK(events[2].event == Event::text);
  CHECK(events[2].payload == "Hello");
  CHECK(events[3].payload == "next");

  // Control frames are never fragmented and carry at most 125 bytes.
  CHECK(failure_of(control(9, bytes_of("p"), false)) == FrameFailure::fragmented_control);
  CHECK(failure_of(control(8, {}, false)) == FrameFailure::fragmented_control);
  CHECK(failure_of(control(9, std::vector<std::uint8_t>(125, 'p'))) == FrameFailure::none);
  CHECK(failure_of(control(9, std::vector<std::uint8_t>(126, 'p'))) == FrameFailure::control_too_long);
  CHECK(failure_code(FrameFailure::control_too_long) == 1002);

  // Empty frames are frames.
  const std::vector<Decoded> empty = events_of(join({text_frame("", false), text_frame("", true, 0), text_frame("")}));
  REQUIRE(empty.size() == 2);
  CHECK(empty[0].payload.empty());
  CHECK(empty[1].event == Event::text);
}

TEST_CASE("text is UTF-8, checked across fragments and failed at its first bad byte") {
  // A character split across two fragments is fine.
  const std::vector<Decoded> split = events_of(join({text_frame("caf\xC3", false), text_frame("\xA9", true, 0)}));
  REQUIRE(split.size() == 1);
  CHECK(split[0].payload == "caf\xC3\xA9");
  // A text that ends inside a character fails at its end.
  CHECK(failure_of(text_frame("caf\xC3")) == FrameFailure::invalid_utf8);
  CHECK(failure_of(join({text_frame("caf", false), text_frame("\xC3", true, 0)})) == FrameFailure::invalid_utf8);
  // An overlong form or a surrogate fails at once, before the frame's end.
  const std::vector<std::uint8_t> overlong = text_frame("ab\xC0\xAF" + std::string(100, 'x'));
  const Decoded early = first_event(overlong);
  CHECK(early.event == Event::failed);
  CHECK(early.failure == FrameFailure::invalid_utf8);
  CHECK(early.code == 1007);
  CHECK(early.consumed == 2 + 4 + 3);  // the header, the mask and "ab\xC0"
  CHECK(failure_of(text_frame("\xED\xA0\x80")) == FrameFailure::invalid_utf8);
  CHECK(failure_of(text_frame("\xF4\x90\x80\x80")) == FrameFailure::invalid_utf8);
  // Binary is never checked.
  FrameSpec binary;
  binary.opcode = 2;
  binary.payload = {0xC0, 0xAF, 0xFF};
  CHECK(failure_of(frame(binary)) == FrameFailure::none);
}

TEST_CASE("Close: a code that may be sent, and a UTF-8 reason") {
  const Decoded none = first_event(control(8, {}));
  CHECK(none.event == Event::close);
  CHECK(none.code == 1005);
  const Decoded normal = first_event(close_frame(1000, "bye"));
  CHECK(normal.event == Event::close);
  CHECK(normal.code == 1000);
  CHECK(normal.payload == "bye");
  for (const std::uint16_t code : {1000, 1001, 1002, 1003, 1007, 1008, 1009, 1010, 1011, 1012, 1013, 1014, 3000, 3999,
                                   4000, 4999}) {
    CAPTURE(code);
    CHECK(first_event(close_frame(code)).event == Event::close);
  }
  for (const std::uint16_t code : {0, 999, 1004, 1005, 1006, 1015, 1016, 1100, 2000, 2999, 5000, 65535}) {
    CAPTURE(code);
    CHECK(failure_of(close_frame(code)) == FrameFailure::bad_close_code);
  }
  CHECK(failure_of(control(8, {0x03})) == FrameFailure::close_one_byte);
  CHECK(failure_of(close_frame(1000, "\xC3")) == FrameFailure::bad_close_reason);
  CHECK(failure_code(FrameFailure::bad_close_reason) == 1007);
  CHECK(failure_of(close_frame(1000, std::string(123, 'r'))) == FrameFailure::none);

  // After Close the decoder takes everything and reports nothing more.
  const std::vector<Decoded> after = events_of(join({close_frame(1000), text_frame("ignored")}));
  REQUIRE(after.size() == 1);
  CHECK(after[0].event == Event::close);
}

TEST_CASE("the encoder: FIN set, never masked, minimal lengths, and refusals") {
  std::array<std::uint8_t, 16> small{};
  CHECK(encode_frame_header(Opcode::text, 0, small) == 2);
  CHECK(small[0] == 0x81);
  CHECK(small[1] == 0x00);
  CHECK(encode_frame_header(Opcode::text, 125, small) == 2);
  CHECK(small[1] == 125);
  CHECK(encode_frame_header(Opcode::text, 126, small) == 4);
  CHECK(encode_frame_header(Opcode::text, 0xFFFF, small) == 4);
  CHECK(encode_frame_header(Opcode::text, 0x10000, small) == 10);
  CHECK(encode_frame_header(Opcode::continuation, 1, small) == 0);
  CHECK(encode_frame_header(static_cast<Opcode>(3), 1, small) == 0);
  CHECK(encode_frame_header(Opcode::ping, 126, small) == 0);
  CHECK(encode_frame_header(Opcode::binary, std::uint64_t{1} << 63, small) == 0);
  CHECK(encode_frame_header(Opcode::binary, 0x10000, std::span<std::uint8_t>(small.data(), 9)) == 0);
  CHECK(encode_frame(Opcode::text, std::vector<std::uint8_t>{0xC3}, small) == 0);  // not UTF-8
  CHECK(encode_frame(Opcode::binary, std::vector<std::uint8_t>(15, 1), small) == 0);  // no room

  CHECK(encode_close(1000, "", small) == 4);
  CHECK(small[0] == 0x88);
  CHECK(small[1] == 2);
  CHECK(small[2] == 0x03);
  CHECK(small[3] == 0xE8);
  CHECK(encode_close(1005, "", small) == 0);
  CHECK(encode_close(1006, "", small) == 0);
  CHECK(encode_close(1000, "\xC3", small) == 0);
  std::array<std::uint8_t, kMaxControlFrame> room{};
  CHECK(encode_close(1000, std::string(123, 'r'), room) == kMaxControlFrame);
  CHECK(encode_close(1000, std::string(124, 'r'), room) == 0);
}

TEST_CASE("round trips: what the server encodes, a page masks, and the decoder reads back") {
  std::vector<std::uint8_t> out(kMaxMessage + 16);
  for (const std::size_t size : {0u, 1u, 125u, 126u, 127u, 1000u, 4096u, 16384u}) {
    CAPTURE(size);
    std::vector<std::uint8_t> payload(size);
    for (std::size_t i = 0; i < size; ++i) {
      payload[i] = static_cast<std::uint8_t>('a' + i % 26);
    }
    for (const Opcode opcode : {Opcode::text, Opcode::binary}) {
      const std::size_t written = encode_frame(opcode, payload, out);
      REQUIRE(written != 0);
      const Decoded back = first_event(as_client(std::span<const std::uint8_t>(out.data(), written)));
      CHECK(back.event == (opcode == Opcode::text ? Event::text : Event::binary));
      CHECK(back.payload == text_of(payload));
    }
  }
  std::array<std::uint8_t, kMaxControlFrame> control_out{};
  const std::size_t ping = encode_frame(Opcode::ping, std::vector<std::uint8_t>(125, 'q'), control_out);
  const Decoded pinged = first_event(as_client(std::span<const std::uint8_t>(control_out.data(), ping)));
  CHECK(pinged.payload == std::string(125, 'q'));
  const std::size_t close = encode_close(4000, "done", control_out);
  const Decoded closed = first_event(as_client(std::span<const std::uint8_t>(control_out.data(), close)));
  CHECK(closed.code == 4000);
  CHECK(closed.payload == "done");
}

TEST_CASE("the session: Ping answered, Close echoed once, failures answered, and a server Close awaited") {
  std::array<std::uint8_t, kMaxControlFrame> out{};
  std::size_t consumed = 0;
  std::size_t written = 0;

  Session session;
  const std::vector<std::uint8_t> stream =
      join({control(9, bytes_of("hi")), text_frame("one"), control(10, bytes_of("x")), text_frame("two")});
  std::span<const std::uint8_t> in(stream);
  CHECK(session.on_input(in, consumed, out, written) == Step::send);
  CHECK(text_of(std::span<const std::uint8_t>(out.data(), written)) == "\x8A\x02hi");  // the Pong, unmasked
  in = in.subspan(consumed);
  CHECK(session.on_input(in, consumed, out, written) == Step::message);
  CHECK(session.message_text());
  CHECK(text_of(session.message()) == "one");
  in = in.subspan(consumed);
  CHECK(session.on_input(in, consumed, out, written) == Step::message);  // the Pong is passed over
  CHECK(text_of(session.message()) == "two");
  in = in.subspan(consumed);
  CHECK(in.empty());
  CHECK(session.on_input(in, consumed, out, written) == Step::need_more);

  // The page's Close is echoed with its code, once; then the session is closed for good.
  const std::vector<std::uint8_t> bye = join({close_frame(1001, "tab closed"), text_frame("late")});
  CHECK(session.on_input(bye, consumed, out, written) == Step::close);
  CHECK(consumed == bye.size());
  CHECK(text_of(std::span<const std::uint8_t>(out.data(), written)) == "\x88\x02\x03\xE9");
  CHECK(session.state() == Session::State::closed);
  CHECK(session.close_code() == 1001);
  CHECK(session.on_input(bye, consumed, out, written) == Step::close);
  CHECK(written == 0);
  CHECK(session.start_close(1000, out) == 0);

  // A Close with no code is echoed with none.
  Session quiet;
  CHECK(quiet.on_input(control(8, {}), consumed, out, written) == Step::close);
  CHECK(written == 2);
  CHECK(out[0] == 0x88);
  CHECK(out[1] == 0x00);

  // A failure is answered with its code.
  Session failing;
  FrameSpec unmasked;
  unmasked.masked = false;
  CHECK(failing.on_input(frame(unmasked), consumed, out, written) == Step::close);
  CHECK(text_of(std::span<const std::uint8_t>(out.data(), written)) == "\x88\x02\x03\xEA");  // 1002
  CHECK(failing.failure() == FrameFailure::unmasked);
  Session too_big;
  FrameSpec big;
  big.claimed = kMaxMessage + 1;
  big.payload.clear();
  CHECK(too_big.on_input(frame(big), consumed, out, written) == Step::close);
  CHECK(text_of(std::span<const std::uint8_t>(out.data(), written)) == "\x88\x02\x03\xF1");  // 1009

  // The server's Close: sent once, then everything but the page's Close is dropped, and that Close is not echoed.
  Session closing;
  const std::size_t sent = closing.start_close(1001, out);
  CHECK(text_of(std::span<const std::uint8_t>(out.data(), sent)) == "\x88\x02\x03\xE9");
  CHECK(closing.state() == Session::State::closing);
  CHECK(closing.start_close(1001, out) == 0);
  const std::vector<std::uint8_t> tail = join({text_frame("dropped"), control(9, bytes_of("p")), close_frame(1000)});
  CHECK(closing.on_input(tail, consumed, out, written) == Step::close);
  CHECK(written == 0);
  CHECK(closing.state() == Session::State::closed);
  Session closing_failure;
  closing_failure.start_close(1000, out);
  CHECK(closing_failure.on_input(frame(unmasked), consumed, out, written) == Step::close);
  CHECK(written == 0);
  CHECK(closing_failure.start_close(4000, out) == 0);
  Session refused;
  CHECK(refused.start_close(1006, out) == 0);  // not a code to send
  CHECK(refused.state() == Session::State::open);
}

TEST_CASE("decoding allocates nothing") {
  const std::vector<std::uint8_t> stream = join({text_frame("He", false), control(9, bytes_of("p")),
                                                 text_frame("llo", true, 0), close_frame(1000, "bye")});
  std::array<std::uint8_t, kMaxControlFrame> out{};
  grace::abi::test::Counting counting;
  Session session;
  std::span<const std::uint8_t> in(stream);
  std::size_t consumed = 0;
  std::size_t written = 0;
  int steps = 0;
  while (session.state() != Session::State::closed && steps < 10) {
    session.on_input(in, consumed, out, written);
    in = in.subspan(consumed);
    ++steps;
  }
  const grace::abi::test::Counts counts = counting.counts();
  CHECK(steps == 3);
  CHECK(counts.allocations == 0);
}
