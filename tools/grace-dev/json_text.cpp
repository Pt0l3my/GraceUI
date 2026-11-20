// SPDX-License-Identifier: MIT
// json_text.cpp: an author's files read and parsed, with lines named (json_text.h).
#include "json_text.h"

#include <fstream>
#include <system_error>

namespace grace::dev {
namespace {

// Finds where JSON fails, without an exception: nlohmann's SAX parser hands the error's byte to this, and stops.
class ErrorFinder final : public nlohmann::json_sax<protocol::Json> {
 public:
  std::size_t position = 0;
  bool null() override { return true; }
  bool boolean(bool) override { return true; }
  bool number_integer(number_integer_t) override { return true; }
  bool number_unsigned(number_unsigned_t) override { return true; }
  bool number_float(number_float_t, const string_t&) override { return true; }
  bool string(string_t&) override { return true; }
  bool binary(binary_t&) override { return true; }
  bool start_object(std::size_t) override { return true; }
  bool key(string_t&) override { return true; }
  bool end_object() override { return true; }
  bool start_array(std::size_t) override { return true; }
  bool end_array() override { return true; }
  bool parse_error(std::size_t at, const std::string&, const nlohmann::detail::exception&) override {
    position = at;
    return false;
  }
};

}  // namespace

std::filesystem::path path_from_utf8(std::string_view text) {
  return std::filesystem::path(std::u8string(text.begin(), text.end()));
}

std::string name_of(const std::filesystem::path& path) {
  std::filesystem::path last = path.filename();
  if (last.empty()) {
    last = path.parent_path().filename();  // a folder given with a separator at its end
  }
  const std::u8string text = last.u8string();
  return std::string(text.begin(), text.end());
}

bool read_file(const std::filesystem::path& path, std::size_t max, std::string& out, std::string& why) {
  std::error_code error;
  if (!std::filesystem::is_regular_file(path, error) || error) {
    why = "missing";
    return false;
  }
  const std::uintmax_t size = std::filesystem::file_size(path, error);
  if (error) {
    why = "unreadable";
    return false;
  }
  if (size > max) {
    why = "larger than " + std::to_string(max / 1024) + " KiB";
    return false;
  }
  std::ifstream in(path, std::ios::binary);
  out.assign(static_cast<std::size_t>(size), '\0');
  if (!in || !in.read(out.data(), static_cast<std::streamsize>(out.size()))) {
    why = "unreadable";
    return false;
  }
  return true;
}

std::size_t line_at(std::string_view text, std::size_t offset) noexcept {
  std::size_t line = 1;
  for (std::size_t i = 0; i < offset && i < text.size(); ++i) {
    line += text[i] == '\n' ? 1 : 0;
  }
  return line;
}

bool parse_json(std::string_view text, protocol::Json& out, std::string& why) {
  try {
    out = protocol::Json::parse(text.begin(), text.end(), nullptr, false);
    if (!out.is_discarded()) {
      return true;
    }
    ErrorFinder finder;
    protocol::Json::sax_parse(text.begin(), text.end(), &finder);
    why = "line " + std::to_string(line_at(text, finder.position > 0 ? finder.position - 1 : 0)) + ": not JSON";
  } catch (...) {
    why = "not JSON";
  }
  out = protocol::Json();
  return false;
}

std::vector<std::size_t> element_lines(std::string_view text, std::string_view key) {
  std::vector<std::size_t> lines;
  std::size_t line = 1;
  int depth = 0;
  bool in_string = false;
  bool escape = false;
  bool expect_key = false;   // at depth 1, in an object: the next string is a member's name
  bool reading_key = false;  // the string being read is that name
  std::string current;
  std::string last_key;
  bool in_target = false;    // inside the list that is `key`'s value, whose elements are at depth 2
  bool want = false;         // the next value there starts an element
  for (const char c : text) {
    if (c == '\n') {
      ++line;
    }
    if (in_string) {
      if (escape) {
        escape = false;
        if (reading_key) {
          current.push_back(c);
        }
      } else if (c == '\\') {
        escape = true;
      } else if (c == '"') {
        in_string = false;
        if (reading_key) {
          reading_key = false;
          last_key = current;
        }
      } else if (reading_key) {
        current.push_back(c);
      }
      continue;
    }
    if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
      continue;
    }
    if (in_target && depth == 2) {
      if (c == ',') {
        want = true;
        continue;
      }
      if (c == ']') {
        in_target = false;
        --depth;
        continue;
      }
      if (want) {
        lines.push_back(line);
        want = false;
      }
    }
    switch (c) {
      case '{':
      case '[':
        if (depth == 1 && c == '[' && !expect_key && last_key == key) {
          lines.clear();  // the last member of that name is the one the parser keeps
          in_target = true;
          want = true;
        }
        ++depth;
        if (depth == 1) {
          expect_key = c == '{';
        }
        break;
      case '}':
      case ']':
        --depth;
        break;
      case '"':
        in_string = true;
        if (depth == 1 && expect_key) {
          reading_key = true;
          current.clear();
        }
        break;
      case ':':
        if (depth == 1) {
          expect_key = false;
        }
        break;
      case ',':
        if (depth == 1) {
          expect_key = true;
        }
        break;
      default:
        break;
    }
  }
  return lines;
}

}  // namespace grace::dev
