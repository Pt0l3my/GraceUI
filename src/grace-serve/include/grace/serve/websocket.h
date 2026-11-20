// websocket.h: the WebSocket handshake (RFC 6455 §4.2) and frames (§5), strict, as §7.2 item 22 has them: the upgrade
// request's check and Sec-WebSocket-Accept; a decoder of a page's frames, which must be masked, with no reserved bit,
// minimal lengths, control frames of at most 125 bytes and never fragmented, a message reassembled into a fixed buffer of
// kMaxMessage bytes (its cap checked from each frame's header, before any payload), text checked as UTF-8 across its
// fragments, and Close's code and reason checked; the encoder of the server's frames, never masked, never fragmented;
// and the session's state machine for Ping, Pong and Close. Pure: no sockets, no clocks, no allocation. The deadlines
// (the ticket's 2 s from the 101, Close's bounded wait), the send queue and the command rate are the I/O part's.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

#include "grace/serve/codec.h"
#include "grace/serve/head.h"
#include "grace/serve/limits.h"
#include "grace/serve/refusal.h"

namespace grace::serve {

// The upgrade request (§4.2.1): a GET with Upgrade: websocket (without case), a Connection holding the token "upgrade",
// Sec-WebSocket-Version: 13 (426 otherwise, or 400 when missing), a Sec-WebSocket-Key that decodes to 16 bytes, no body
// and no byte after the head (`bytes_after_head`: what the connection holds past head.size before the 101 is sent). No
// Upgrade at all is 426. Extensions and subprotocols are ignored, so none is ever negotiated. The Host, Origin, Fetch
// Metadata and cookie checks come first (check_api_request). On none, `accept` holds Sec-WebSocket-Accept.
Refusal check_upgrade(const RequestHead& head, std::size_t bytes_after_head,
                      std::span<char, kAcceptChars> accept) noexcept;

// Sec-WebSocket-Accept for a key: Base64 of the SHA-1 of the key and RFC 6455's GUID.
void compute_accept(std::string_view key, std::span<char, kAcceptChars> accept) noexcept;

enum class Opcode : std::uint8_t { continuation = 0, text = 1, binary = 2, close = 8, ping = 9, pong = 10 };

// Why a page's frames failed the connection; failure_code() gives the Close code to answer with.
enum class FrameFailure : std::uint8_t {
  none,
  reserved_bits,        // 1002: RSV1, RSV2 or RSV3 set, and no extension is ever negotiated
  reserved_opcode,      // 1002
  unmasked,             // 1002: a client's frame must be masked
  fragmented_control,   // 1002
  control_too_long,     // 1002: a control frame's payload past 125 bytes
  stray_continuation,   // 1002: a continuation with no message open
  interleaved_message,  // 1002: a new text or binary frame while a message is open
  non_minimal_length,   // 1002: a length in a longer form than it needs
  length_high_bit,      // 1002: a 64-bit length with its top bit set
  too_big,              // 1009: the message past kMaxMessage, seen from a frame's header
  invalid_utf8,         // 1007: a text message that is not UTF-8, found at its first bad byte
  close_one_byte,       // 1002: a Close body of one byte
  bad_close_code,       // 1002: a code that may not be sent on the wire
  bad_close_reason,     // 1007: a Close reason that is not UTF-8
};

std::uint16_t failure_code(FrameFailure failure) noexcept;

// A Close code that may be sent and received on the wire: 1000 to 1003, 1007 to 1014, and 3000 to 4999.
bool close_code_allowed(std::uint16_t code) noexcept;

// A page's frames, decoded incrementally from the connection's bytes.
class FrameDecoder {
 public:
  enum class Event : std::uint8_t {
    need_more,  // every byte given was taken, and no event is complete
    text,       // a whole text message: payload()
    binary,     // a whole binary message: payload()
    ping,       // payload(): what the Pong must carry
    pong,
    close,      // the peer's Close: close_code() (1005 when it carried none) and payload(), its reason; then finished
    failed,     // failure() and close_code(), the code to answer with; then finished
  };

  FrameDecoder() noexcept = default;
  FrameDecoder(const FrameDecoder&) = delete;
  FrameDecoder& operator=(const FrameDecoder&) = delete;

  // Takes bytes from `in` until one event is complete or `in` is spent, and says in `consumed` how many it took. Once
  // finished, it takes everything and reports nothing.
  Event feed(std::span<const std::uint8_t> in, std::size_t& consumed) noexcept;

  // The last event's payload: valid until the next call of feed.
  std::span<const std::uint8_t> payload() const noexcept { return payload_; }
  std::uint16_t close_code() const noexcept { return close_code_; }
  FrameFailure failure() const noexcept { return failure_; }
  bool finished() const noexcept { return finished_; }

 private:
  enum class Stage : std::uint8_t { head, length, mask, payload };
  Event start_frame() noexcept;
  Event accept_length() noexcept;
  Event end_frame() noexcept;
  Event end_close() noexcept;
  Event fail(FrameFailure failure) noexcept;

  Stage stage_ = Stage::head;
  std::uint8_t have_ = 0;  // bytes of the current header part
  std::uint8_t need_ = 0;  // bytes of the extended length
  std::array<std::uint8_t, 8> part_{};
  std::array<std::uint8_t, 4> mask_{};
  bool fin_ = false;
  std::uint8_t opcode_ = 0;
  std::uint64_t length_ = 0;
  std::uint64_t received_ = 0;
  bool in_message_ = false;
  bool message_text_ = false;
  std::size_t message_size_ = 0;
  Utf8Stream utf8_;
  bool finished_ = false;
  FrameFailure failure_ = FrameFailure::none;
  std::uint16_t close_code_ = 0;
  std::span<const std::uint8_t> payload_;
  std::array<std::uint8_t, kMaxControlPayload> control_{};
  std::array<std::uint8_t, kMaxMessage> message_{};
};

// The server's frames: FIN set, never masked, the length in its minimal form (RFC 6455 §5.2). A continuation is never
// written (every message goes whole), nor a control frame past 125 bytes. Each returns the bytes written, or 0.
std::size_t encode_frame_header(Opcode opcode, std::uint64_t length, std::span<std::uint8_t> out) noexcept;
// The header and the payload; a text payload must be UTF-8.
std::size_t encode_frame(Opcode opcode, std::span<const std::uint8_t> payload, std::span<std::uint8_t> out) noexcept;
// A Close with a code close_code_allowed() takes and a reason of at most 123 bytes of UTF-8.
std::size_t encode_close(std::uint16_t code, std::string_view reason, std::span<std::uint8_t> out) noexcept;

// One connection after its 101: Ping answered with Pong, Close echoed once, every failure answered with its Close code.
class Session {
 public:
  enum class State : std::uint8_t {
    open,
    closing,  // the server sent its Close and waits, within the I/O part's bounded wait, for the page's
    closed,   // nothing more is read or sent: shut the socket down with a bounded drain
  };
  enum class Step : std::uint8_t {
    need_more,  // every byte given was taken
    message,    // a whole message: message_text() and message(), valid until the next call
    send,       // send out[0, written): a Pong
    close,      // send out[0, written), which may be nothing, then shut down; the session is closed
  };

  Session() noexcept = default;
  Session(const Session&) = delete;
  Session& operator=(const Session&) = delete;

  // `out` must hold kMaxControlFrame bytes. While closing, messages, Pings and Pongs are dropped.
  Step on_input(std::span<const std::uint8_t> in, std::size_t& consumed, std::span<std::uint8_t> out,
                std::size_t& written) noexcept;

  // The server's own Close, once, while open: writes it into `out` and returns its size (0 if not open, a code not
  // allowed, or too little room). Afterwards nothing but the page's Close is awaited.
  std::size_t start_close(std::uint16_t code, std::span<std::uint8_t> out) noexcept;

  State state() const noexcept { return state_; }
  bool message_text() const noexcept { return text_; }
  std::span<const std::uint8_t> message() const noexcept { return decoder_.payload(); }
  // Once closed by the page or a failure: the page's code (1005 for none) or the failure's.
  std::uint16_t close_code() const noexcept { return decoder_.close_code(); }
  FrameFailure failure() const noexcept { return decoder_.failure(); }

 private:
  State state_ = State::open;
  bool text_ = false;
  FrameDecoder decoder_;
};

}  // namespace grace::serve
