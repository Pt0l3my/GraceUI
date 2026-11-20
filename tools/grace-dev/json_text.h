// SPDX-License-Identifier: MIT
// json_text.h: grace-dev's reading of an author's files (D168): a file read whole within a bound, JSON parsed with the
// line of its first error named, and the line each element of a top-level list starts on, so that a bad scenario step
// is named by its line. Messages name a file by its name alone, never its folder (PV3).
#pragma once

#include <cstddef>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include "grace/protocol/codec.h"

namespace grace::dev {

// A path from UTF-8 (the command line's, as main hands it over), and a path's last part as UTF-8, for messages.
std::filesystem::path path_from_utf8(std::string_view text);
std::string name_of(const std::filesystem::path& path);

// The whole file, at most `max` bytes: false with `why` ("missing", "larger than ...", "unreadable").
bool read_file(const std::filesystem::path& path, std::size_t max, std::string& out, std::string& why);

// `text` as JSON: false with `why` naming the line of the first error ("line 7: not JSON").
bool parse_json(std::string_view text, protocol::Json& out, std::string& why);

// The line (from 1) each element of the list that is the top-level object's member `key` starts on, in order; empty if
// there is no such list. `text` must already have parsed as JSON.
std::vector<std::size_t> element_lines(std::string_view text, std::string_view key);

// The line (from 1) holding byte `offset` of `text`.
std::size_t line_at(std::string_view text, std::size_t offset) noexcept;

}  // namespace grace::dev
