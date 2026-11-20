// SPDX-License-Identifier: MIT
// cli.h: grace-dev's command line (tools/grace-dev/README.md): serving spaces, or dumping one page's in-game layout.
// Each value is checked as it is read, the port, the scale and the theme by GraceUI's own settings' descriptions.
#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "dump.h"
#include "host.h"

namespace grace::dev {

struct Cli {
  enum class Mode : std::uint8_t { serve, dump, help };
  Mode mode = Mode::serve;
  HostOptions host;
  std::string dump_space;  // --dump SPACE PAGE: a built-in space's id, or a --space-dir folder's
  DumpRequest dump;
};

// The arguments after the program's name, as UTF-8: false with `why` for one that fails.
bool parse_cli(const std::vector<std::string>& args, Cli& out, std::string& why);

std::string_view usage() noexcept;

}  // namespace grace::dev
