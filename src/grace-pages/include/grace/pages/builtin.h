// SPDX-License-Identifier: MIT
// builtin.h: GraceUI's built-in spaces (D79, D82, D160): its own space and the demo space, compiled into grace-pages as
// the protocol's JSON, their page data (space_pages) and settings descriptions (settings_description), each checked by
// the page lint (tools/pages/build.py) before it was written. Pure: no I/O.
#pragma once

#include <span>
#include <string_view>

namespace grace::pages {

struct BuiltinSpace {
  std::string_view id;
  std::string_view pages;     // the protocol's space_pages, as JSON
  std::string_view settings;  // the protocol's settings_description, as JSON
};

// GraceUI's own space first, then the demo space.
std::span<const BuiltinSpace> builtin_spaces() noexcept;

}  // namespace grace::pages
