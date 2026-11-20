// frames.cpp: the frame decoder, the server's frame encoder and the session's state machine (RFC 6455 §5, §7;
// websocket.h).
#include <algorithm>

#include "grace/serve/websocket.h"

namespace grace::serve {
namespace {

constexpr std::uint8_t kFin = 0x80;
constexpr std::uint8_t kRsv = 0x70;
constexpr std::uint8_t kMaskBit = 0x80;
constexpr std::size_t kMaxCloseReason = kMaxControlPayload - 2;

constexpr bool is_control(std::uint8_t opcode) noexcept { return opcode >= 8; }

constexpr bool known_opcode(std::uint8_t opcode) noexcept { return opcode <= 2 || (opcode >= 8 && opcode <= 10); }

}  // namespace

std::uint16_t failure_code(FrameFailure failure) noexcept {
  switch (failure) {
    case FrameFailure::none:
      return 0;
    case FrameFailure::too_big:
      return 1009;
    case FrameFailure::invalid_utf8:
    case FrameFailure::bad_close_reason:
      return 1007;
    case FrameFailure::reserved_bits:
    case FrameFailure::reserved_opcode:
    case FrameFailure::unmasked:
    case FrameFailure::fragmented_control:
    case FrameFailure::control_too_long:
    case FrameFailure::stray_continuation:
    case FrameFailure::interleaved_message:
    case FrameFailure::non_minimal_length:
    case FrameFailure::length_high_bit:
    case FrameFailure::close_one_byte:
    case FrameFailure::bad_close_code:
      return 1002;
  }
  return 1002;
}

bool close_code_allowed(std::uint16_t code) noexcept {
  return (code >= 1000 && code <= 1003) || (code >= 1007 && code <= 1014) || (code >= 3000 && code <= 4999);
}

FrameDecoder::Event FrameDecoder::fail(FrameFailure failure) noexcept {
  finished_ = true;
  failure_ = failure;
  close_code_ = failure_code(failure);
  payload_ = {};
  return Event::failed;
}

FrameDecoder::Event FrameDecoder::feed(std::span<const std::uint8_t> in, std::size_t& consumed) noexcept {
  consumed = 0;
  payload_ = {};
  if (finished_) {
    consumed = in.size();
    return Event::need_more;
  }
  while (consumed < in.size()) {
    switch (stage_) {
      case Stage::head: {
        part_[have_++] = in[consumed++];
        if (have_ == 2) {
          const Event event = start_frame();
          if (event != Event::need_more) {
            return event;
          }
        }
        break;
      }
      case Stage::length: {
        part_[have_++] = in[consumed++];
        if (have_ == need_) {
          if (need_ == 2) {
            length_ = (std::uint64_t{part_[0]} << 8) | part_[1];
            if (length_ < 126) {
              return fail(FrameFailure::non_minimal_length);
            }
          } else {
            if ((part_[0] & 0x80) != 0) {
              return fail(FrameFailure::length_high_bit);
            }
            length_ = 0;
            for (std::size_t i = 0; i < 8; ++i) {
              length_ = (length_ << 8) | part_[i];
            }
            if (length_ <= 0xFFFF) {
              return fail(FrameFailure::non_minimal_length);
            }
          }
          const Event event = accept_length();
          if (event != Event::need_more) {
            return event;
          }
        }
        break;
      }
      case Stage::mask: {
        mask_[have_++] = in[consumed++];
        if (have_ == 4) {
          stage_ = Stage::payload;
          received_ = 0;
          if (length_ == 0) {
            const Event event = end_frame();
            if (event != Event::need_more) {
              return event;
            }
          }
        }
        break;
      }
      case Stage::payload: {
        const std::uint64_t left = length_ - received_;
        const std::size_t available = in.size() - consumed;
        const std::size_t take = left < available ? static_cast<std::size_t>(left) : available;
        const bool control = is_control(opcode_);
        // The cap was checked from the header: the frame fits where it goes.
        std::uint8_t* const into = control ? control_.data() : message_.data() + message_size_;
        const bool text = !control && message_text_;
        const auto start = static_cast<std::size_t>(received_);
        for (std::size_t i = 0; i < take; ++i) {
          const std::size_t at = start + i;
          const auto byte = static_cast<std::uint8_t>(in[consumed + i] ^ mask_[at & 3]);
          into[at] = byte;
          if (text && !utf8_.feed(byte)) {
            consumed += i + 1;
            return fail(FrameFailure::invalid_utf8);
          }
        }
        consumed += take;
        received_ += take;
        if (received_ == length_) {
          const Event event = end_frame();
          if (event != Event::need_more) {
            return event;
          }
        }
        break;
      }
    }
  }
  return Event::need_more;
}

// The first two bytes are in: every rule that they alone decide.
FrameDecoder::Event FrameDecoder::start_frame() noexcept {
  const std::uint8_t first = part_[0];
  const std::uint8_t second = part_[1];
  fin_ = (first & kFin) != 0;
  opcode_ = static_cast<std::uint8_t>(first & 0x0F);
  if ((first & kRsv) != 0) {
    return fail(FrameFailure::reserved_bits);
  }
  if (!known_opcode(opcode_)) {
    return fail(FrameFailure::reserved_opcode);
  }
  if ((second & kMaskBit) == 0) {
    return fail(FrameFailure::unmasked);
  }
  const auto length = static_cast<std::uint8_t>(second & 0x7F);
  if (is_control(opcode_)) {
    if (!fin_) {
      return fail(FrameFailure::fragmented_control);
    }
    if (length > kMaxControlPayload) {
      return fail(FrameFailure::control_too_long);
    }
  } else if (opcode_ == static_cast<std::uint8_t>(Opcode::continuation)) {
    if (!in_message_) {
      return fail(FrameFailure::stray_continuation);
    }
  } else {
    if (in_message_) {
      return fail(FrameFailure::interleaved_message);
    }
    message_size_ = 0;
    message_text_ = opcode_ == static_cast<std::uint8_t>(Opcode::text);
    utf8_ = Utf8Stream{};
  }
  have_ = 0;
  if (length == 126 || length == 127) {
    need_ = length == 126 ? 2 : 8;
    stage_ = Stage::length;
    return Event::need_more;
  }
  length_ = length;
  return accept_length();
}

// The frame's length is known: the message's cap, before any of its payload is read (§7.2 item 22).
FrameDecoder::Event FrameDecoder::accept_length() noexcept {
  if (!is_control(opcode_) && message_size_ + length_ > kMaxMessage) {
    return fail(FrameFailure::too_big);
  }
  have_ = 0;
  stage_ = Stage::mask;
  return Event::need_more;
}

FrameDecoder::Event FrameDecoder::end_frame() noexcept {
  stage_ = Stage::head;
  have_ = 0;
  const auto length = static_cast<std::size_t>(length_);
  if (is_control(opcode_)) {
    payload_ = std::span<const std::uint8_t>(control_.data(), length);
    if (opcode_ == static_cast<std::uint8_t>(Opcode::ping)) {
      return Event::ping;
    }
    if (opcode_ == static_cast<std::uint8_t>(Opcode::pong)) {
      return Event::pong;
    }
    return end_close();
  }
  message_size_ += length;
  if (!fin_) {
    in_message_ = true;
    return Event::need_more;
  }
  in_message_ = false;
  if (message_text_ && !utf8_.complete()) {
    return fail(FrameFailure::invalid_utf8);  // the text ends inside a character
  }
  payload_ = std::span<const std::uint8_t>(message_.data(), message_size_);
  return message_text_ ? Event::text : Event::binary;
}

FrameDecoder::Event FrameDecoder::end_close() noexcept {
  if (length_ == 0) {
    finished_ = true;
    close_code_ = 1005;  // no status received (RFC 6455 §7.4.1)
    payload_ = {};
    return Event::close;
  }
  if (length_ == 1) {
    return fail(FrameFailure::close_one_byte);
  }
  const auto code = static_cast<std::uint16_t>((control_[0] << 8) | control_[1]);
  if (!close_code_allowed(code)) {
    return fail(FrameFailure::bad_close_code);
  }
  const std::span<const std::uint8_t> reason(control_.data() + 2, static_cast<std::size_t>(length_) - 2);
  if (!valid_utf8(reason)) {
    return fail(FrameFailure::bad_close_reason);
  }
  finished_ = true;
  close_code_ = code;
  payload_ = reason;
  return Event::close;
}

std::size_t encode_frame_header(Opcode opcode, std::uint64_t length, std::span<std::uint8_t> out) noexcept {
  const auto code = static_cast<std::uint8_t>(opcode);
  if (opcode == Opcode::continuation || !known_opcode(code) || (is_control(code) && length > kMaxControlPayload) ||
      (length >> 63) != 0) {
    return 0;
  }
  const std::size_t size = length < 126 ? 2 : length <= 0xFFFF ? 4 : 10;
  if (out.size() < size) {
    return 0;
  }
  out[0] = static_cast<std::uint8_t>(kFin | code);
  if (size == 2) {
    out[1] = static_cast<std::uint8_t>(length);
  } else if (size == 4) {
    out[1] = 126;
    out[2] = static_cast<std::uint8_t>(length >> 8);
    out[3] = static_cast<std::uint8_t>(length);
  } else {
    out[1] = 127;
    for (std::size_t i = 0; i < 8; ++i) {
      out[2 + i] = static_cast<std::uint8_t>(length >> (56 - 8 * i));
    }
  }
  return size;
}

std::size_t encode_frame(Opcode opcode, std::span<const std::uint8_t> payload, std::span<std::uint8_t> out) noexcept {
  if (opcode == Opcode::text && !valid_utf8(payload)) {
    return 0;
  }
  const std::size_t header = encode_frame_header(opcode, payload.size(), out);
  if (header == 0 || out.size() - header < payload.size()) {
    return 0;
  }
  std::copy(payload.begin(), payload.end(), out.begin() + static_cast<std::ptrdiff_t>(header));
  return header + payload.size();
}

std::size_t encode_close(std::uint16_t code, std::string_view reason, std::span<std::uint8_t> out) noexcept {
  if (!close_code_allowed(code) || reason.size() > kMaxCloseReason || !valid_utf8(reason)) {
    return 0;
  }
  const std::size_t header = encode_frame_header(Opcode::close, 2 + reason.size(), out);
  if (header == 0 || out.size() - header < 2 + reason.size()) {
    return 0;
  }
  out[header] = static_cast<std::uint8_t>(code >> 8);
  out[header + 1] = static_cast<std::uint8_t>(code);
  for (std::size_t i = 0; i < reason.size(); ++i) {
    out[header + 2 + i] = static_cast<std::uint8_t>(reason[i]);
  }
  return header + 2 + reason.size();
}

Session::Step Session::on_input(std::span<const std::uint8_t> in, std::size_t& consumed, std::span<std::uint8_t> out,
                                std::size_t& written) noexcept {
  consumed = 0;
  written = 0;
  if (state_ == State::closed) {
    consumed = in.size();
    return Step::close;
  }
  while (true) {
    std::size_t used = 0;
    const FrameDecoder::Event event = decoder_.feed(in.subspan(consumed), used);
    consumed += used;
    switch (event) {
      case FrameDecoder::Event::need_more:
        return Step::need_more;
      case FrameDecoder::Event::text:
      case FrameDecoder::Event::binary:
        if (state_ == State::open) {
          text_ = event == FrameDecoder::Event::text;
          return Step::message;
        }
        break;  // closing: dropped
      case FrameDecoder::Event::ping:
        if (state_ == State::open) {
          written = encode_frame(Opcode::pong, decoder_.payload(), out);
          if (written == 0) {
            state_ = State::closed;  // no room for the Pong: the caller broke the contract, so close
            consumed = in.size();
            return Step::close;
          }
          return Step::send;
        }
        break;
      case FrameDecoder::Event::pong:
        break;
      case FrameDecoder::Event::close:
        if (state_ == State::open) {
          // Echo once: the page's code back, or an empty Close for one that carried none (RFC 6455 §5.5.1).
          written = decoder_.close_code() == 1005 ? encode_frame(Opcode::close, {}, out)
                                                  : encode_close(decoder_.close_code(), {}, out);
        }
        state_ = State::closed;
        consumed = in.size();
        return Step::close;
      case FrameDecoder::Event::failed:
        if (state_ == State::open) {
          written = encode_close(decoder_.close_code(), {}, out);
        }
        state_ = State::closed;
        consumed = in.size();
        return Step::close;
    }
  }
}

std::size_t Session::start_close(std::uint16_t code, std::span<std::uint8_t> out) noexcept {
  if (state_ != State::open) {
    return 0;
  }
  const std::size_t written = encode_close(code, {}, out);
  if (written != 0) {
    state_ = State::closing;
  }
  return written;
}

}  // namespace grace::serve
