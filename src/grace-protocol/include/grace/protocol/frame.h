// SPDX-License-Identifier: MIT
// frame.h: one bridge-protocol frame from text and to text (GraceUI DESIGN §6.3, §6.7, §6.8; D159). Parsing never
// throws (§6.7): a frame that is too large, too deep, not JSON, not an object, of another major, of an unknown kind, sent
// the wrong way, or with a field missing, of the wrong type or past its bound is refused with its error code and the
// field's path, never its value. Pure: no I/O, threads or clocks.
#pragma once

#include <cstddef>
#include <string>
#include <string_view>

#include "grace/protocol/codec.h"
#include "grace/protocol/messages.h"

namespace grace::protocol {

// Which way a frame travels: from a page (a view) to GraceUI and its spaces, or to a page.
enum class Direction : std::uint8_t { from_page, to_page };

// The deepest nesting of objects and lists a frame may hold (EST): deeper is refused before it is parsed.
inline constexpr std::size_t kMaxDepth = 32;

std::size_t frame_limit(Direction direction) noexcept;

struct Refusal {
  ErrorCode code = ErrorCode::bad_message;  // bad_message, version, or internal for a fault inside GraceUI
  DecodeError where;                       // the field and what was wrong with it; never the value
};

// A frame's message: true with `out` set, or false with `refusal` set.
bool parse_frame(std::string_view text, Direction direction, Message& out, Refusal& refusal) noexcept;

// A message as a frame's text, its fields in the schema's order: false if it would be past the direction's limit, or
// if it is not a kind that travels that way.
bool encode_frame(const Message& message, Direction direction, std::string& out) noexcept;

// A framework command's arguments (settings.set and the rest, §6.6): false with `refusal` set if the command is not
// the framework's (unknown_command; its space answers for its own) or its arguments fail (invalid_value).
bool parse_command(const Cmd& cmd, Command& command, CommandArgs& args, Refusal& refusal) noexcept;

}  // namespace grace::protocol
