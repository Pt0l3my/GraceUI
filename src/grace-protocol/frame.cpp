// SPDX-License-Identifier: MIT
// frame.cpp: one frame from text and to text (frame.h).
#include "grace/protocol/frame.h"

namespace grace::protocol {
namespace {

bool refuse(Refusal& refusal, ErrorCode code, const char* field, const char* why) {
  refusal.code = code;
  refusal.where.field = field;
  refusal.where.why = why;
  return false;
}

// The deepest nesting in `text`, counted outside strings, stopping once past `limit`.
std::size_t depth_of(std::string_view text, std::size_t limit) noexcept {
  std::size_t depth = 0;
  std::size_t deepest = 0;
  bool in_string = false;
  bool escaped = false;
  for (const char c : text) {
    if (in_string) {
      if (escaped) {
        escaped = false;
      } else if (c == '\\') {
        escaped = true;
      } else if (c == '"') {
        in_string = false;
      }
      continue;
    }
    if (c == '"') {
      in_string = true;
    } else if (c == '{' || c == '[') {
      if (++depth > deepest) {
        deepest = depth;
        if (deepest > limit) {
          return deepest;
        }
      }
    } else if ((c == '}' || c == ']') && depth > 0) {
      --depth;
    }
  }
  return deepest;
}

bool travels(Kind kind, Direction direction) noexcept {
  const From from = from_of(kind);
  if (from == From::both) {
    return true;
  }
  return direction == Direction::from_page ? from == From::page : from != From::page;
}

}  // namespace

std::size_t frame_limit(Direction direction) noexcept {
  return direction == Direction::from_page ? kFrameFromPage : kFrameToPage;
}

bool parse_frame(std::string_view text, Direction direction, Message& out, Refusal& refusal) noexcept {
  try {
    refusal = Refusal{};
    if (text.size() > frame_limit(direction)) {
      return refuse(refusal, ErrorCode::bad_message, "", "past the frame's limit");
    }
    if (!valid_utf8(text)) {
      return refuse(refusal, ErrorCode::bad_message, "", "not UTF-8");
    }
    if (depth_of(text, kMaxDepth) > kMaxDepth) {
      return refuse(refusal, ErrorCode::bad_message, "", "nested too deeply");
    }
    Json frame = Json::parse(text.begin(), text.end(), nullptr, false);
    if (frame.is_discarded()) {
      return refuse(refusal, ErrorCode::bad_message, "", "not JSON");
    }
    if (!frame.is_object()) {
      return refuse(refusal, ErrorCode::bad_message, "", "not an object");
    }
    const auto v = frame.find("v");
    if (v == frame.end() || !v->is_number_unsigned()) {
      return refuse(refusal, ErrorCode::bad_message, "v", "missing, or not a version");
    }
    if (v->get<std::uint64_t>() != kProtocolMajor) {
      return refuse(refusal, ErrorCode::version, "v", "another major version");
    }
    const auto t = frame.find("t");
    Kind kind{};
    if (t == frame.end() || !t->is_string() || !from_string(t->get_ref<const std::string&>(), kind)) {
      return refuse(refusal, ErrorCode::bad_message, "t", "missing, or not a kind");
    }
    if (!travels(kind, direction)) {
      return refuse(refusal, ErrorCode::bad_message, "t", "a kind that does not travel this way");
    }
    DecodeError error;
    if (!decode_message(kind, frame, out, error)) {
      refusal.code = ErrorCode::bad_message;
      refusal.where = std::move(error);
      return false;
    }
    if (const auto* result = std::get_if<Result>(&out)) {
      // A result answers ok or names an error: exactly one of them (§6.3).
      const bool ok = result->ok.value_or(false);
      if (ok == result->error.has_value() || (result->ok.has_value() && !ok)) {
        return refuse(refusal, ErrorCode::bad_message, "ok", "a result is ok: true or names an error, not both");
      }
    }
    return true;
  } catch (...) {
    return refuse(refusal, ErrorCode::internal, "", "a fault inside GraceUI");
  }
}

bool encode_frame(const Message& message, Direction direction, std::string& out) noexcept {
  try {
    if (!travels(kind_of(message), direction)) {
      return false;
    }
    encode_text(message, out);
    return out.size() <= frame_limit(direction);
  } catch (...) {
    return false;
  }
}

bool parse_command(const Cmd& cmd, Command& command, CommandArgs& args, Refusal& refusal) noexcept {
  try {
    refusal = Refusal{};
    if (!from_string(cmd.name, command)) {
      return refuse(refusal, ErrorCode::unknown_command, "name", "not a command of the framework's");
    }
    DecodeError error;
    if (!decode_args(command, cmd.args, args, error)) {
      refusal.code = ErrorCode::invalid_value;
      refusal.where = std::move(error);
      return false;
    }
    return true;
  } catch (...) {
    return refuse(refusal, ErrorCode::internal, "", "a fault inside GraceUI");
  }
}

}  // namespace grace::protocol
