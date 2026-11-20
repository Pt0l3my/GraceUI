// SPDX-License-Identifier: MIT
// cli.cpp: grace-dev's command line (cli.h).
#include "cli.h"

#include <cmath>
#include <cstdlib>

#include "grace/pages/settings_store.h"
#include "json_text.h"

namespace grace::dev {
namespace {

bool parse_number(const std::string& text, double& out) {
  if (text.empty() || text.size() > 32) {
    return false;
  }
  char* end = nullptr;
  out = std::strtod(text.c_str(), &end);
  return end == text.c_str() + text.size() && std::isfinite(out);
}

bool parse_whole(const std::string& text, std::uint64_t& out) {
  if (text.empty() || text.size() > 9) {
    return false;
  }
  out = 0;
  for (const char c : text) {
    if (c < '0' || c > '9') {
      return false;
    }
    out = out * 10 + static_cast<std::uint64_t>(c - '0');
  }
  return true;
}

// `value` as GraceUI's own setting `key` takes it: its type, bounds, step or choices (one description, D8).
bool as_setting_takes(std::string_view key, const protocol::Json& value, std::string& why) {
  std::string bad;
  if (!pages::valid_value(graceui_setting(key), value, bad)) {
    why = bad + " (as " + std::string(key) + " takes it)";
    return false;
  }
  return true;
}

bool parse_size(const std::string& text, float& width, float& height) {
  const std::size_t x = text.find('x');
  std::uint64_t w = 0;
  std::uint64_t h = 0;
  if (x == std::string::npos || !parse_whole(text.substr(0, x), w) || !parse_whole(text.substr(x + 1), h) || w < 64 ||
      h < 64 || w > 16384 || h > 16384) {
    return false;
  }
  width = static_cast<float>(w);
  height = static_cast<float>(h);
  return true;
}

bool valid_increment(const std::string& text) {
  const std::size_t dot = text.find('.');
  std::uint64_t part = 0;
  return text.size() <= 16 && dot != std::string::npos && parse_whole(text.substr(0, dot), part) &&
         parse_whole(text.substr(dot + 1), part);
}

}  // namespace

std::string_view usage() noexcept {
  return "grace-dev: GraceUI's fake host. It serves spaces from their page data and fixtures on loopback, through\n"
         "the real grace-serve, and dumps a page's in-game layout (tools/grace-dev/README.md).\n"
         "\n"
         "  grace-dev [--space ID]... [--space-dir DIR]... [--scenario FILE]... [--no-scenario]\n"
         "            [--speed X] [--loop] [--port N] [--web DIR] [--increment X.Y]\n"
         "  grace-dev --dump SPACE PAGE --size WxH [--scale S] [--theme grace|clear] [--space-dir DIR]...\n"
         "\n"
         "  --space ID         a built-in space: graceui or demo (with none given, both)\n"
         "  --space-dir DIR    an author's space: pages.json, settings.json, and topics.json and scenario.json if any\n"
         "  --scenario FILE    play FILE in the space it names, instead of that space's own scenario\n"
         "  --no-scenario      the spaces' own scenarios stay still (--scenario files still play)\n"
         "  --speed X          play scenarios X times as fast, from 0.01 to 1000 (default 1)\n"
         "  --loop             play every scenario again and again\n"
         "  --port N           the port on 127.0.0.1 and ::1 (default: GraceUI's dashboard.port, 8642)\n"
         "  --web DIR          the web app: a GraceUI folder whose graceui.toml lists its files, or a plain folder\n"
         "                     (such as web/dist) served from memory; with none, a placeholder page\n"
         "  --increment X.Y    the increment welcome names for every space (default 0.2)\n"
         "  --dump SPACE PAGE  print PAGE's in-game structure and its draw list's counts, and exit\n"
         "  --size WxH         the display's size for --dump\n"
         "  --scale S          GraceUI's ui.scale for --dump, from 0.75 to 1.5 (default GraceUI's, 1.25)\n"
         "  --theme T          grace or clear, for --dump (default grace)\n"
         "\n"
         "Stop it with Ctrl+C. It never prints a setting's value, a cookie or a ticket.\n";
}

bool parse_cli(const std::vector<std::string>& args, Cli& out, std::string& why) {
  out = Cli{};
  out.dump.ui_scale = default_ui_scale();
  bool serving_only = false;
  bool dump_only = false;
  bool sized = false;
  for (std::size_t i = 0; i < args.size(); ++i) {
    const std::string& flag = args[i];
    const auto value = [&](std::size_t count = 1) {
      if (i + count >= args.size()) {
        why = flag + " needs " + (count == 1 ? std::string("a value") : std::to_string(count) + " values");
        return false;
      }
      return true;
    };
    if (flag == "--help" || flag == "-h") {
      out.mode = Cli::Mode::help;
      return true;
    }
    if (flag == "--space") {
      if (!value()) {
        return false;
      }
      out.host.spaces.push_back(SpaceSource{args[++i], {}});
      serving_only = true;
    } else if (flag == "--space-dir") {
      if (!value()) {
        return false;
      }
      out.host.spaces.push_back(SpaceSource{{}, path_from_utf8(args[++i])});
    } else if (flag == "--scenario") {
      if (!value()) {
        return false;
      }
      out.host.scenarios.push_back(path_from_utf8(args[++i]));
      serving_only = true;
    } else if (flag == "--no-scenario") {
      out.host.no_scenario = true;
      serving_only = true;
    } else if (flag == "--speed") {
      if (!value()) {
        return false;
      }
      if (!parse_number(args[++i], out.host.speed) || out.host.speed < kMinSpeed || out.host.speed > kMaxSpeed) {
        why = "--speed is a number from 0.01 to 1000";
        return false;
      }
      serving_only = true;
    } else if (flag == "--loop") {
      out.host.loop = true;
      serving_only = true;
    } else if (flag == "--port") {
      std::uint64_t port = 0;
      if (!value()) {
        return false;
      }
      if (!parse_whole(args[++i], port)) {
        why = "--port is a whole number";
        return false;
      }
      if (!as_setting_takes("dashboard.port", protocol::Json(port), why)) {
        why = "--port: " + why;
        return false;
      }
      out.host.port = static_cast<std::uint16_t>(port);
      serving_only = true;
    } else if (flag == "--web") {
      if (!value()) {
        return false;
      }
      out.host.web = path_from_utf8(args[++i]);
      serving_only = true;
    } else if (flag == "--increment") {
      if (!value()) {
        return false;
      }
      out.host.increment = args[++i];
      if (!valid_increment(out.host.increment)) {
        why = "--increment is a version's major and minor, such as 0.2";
        return false;
      }
      serving_only = true;
    } else if (flag == "--dump") {
      if (!value(2)) {
        return false;
      }
      out.mode = Cli::Mode::dump;
      out.dump_space = args[++i];
      out.dump.page = args[++i];
    } else if (flag == "--size") {
      if (!value()) {
        return false;
      }
      if (!parse_size(args[++i], out.dump.width, out.dump.height)) {
        why = "--size is WxH, each from 64 to 16384 pixels, such as 1920x1080";
        return false;
      }
      sized = true;
      dump_only = true;
    } else if (flag == "--scale") {
      if (!value()) {
        return false;
      }
      if (!parse_number(args[++i], out.dump.ui_scale)) {
        why = "--scale is a number";
        return false;
      }
      if (!as_setting_takes("ui.scale", protocol::Json(out.dump.ui_scale), why)) {
        why = "--scale: " + why;
        return false;
      }
      dump_only = true;
    } else if (flag == "--theme") {
      if (!value()) {
        return false;
      }
      const std::string theme = args[++i];
      if (!as_setting_takes("ui.menu.theme", protocol::Json(theme), why)) {
        why = "--theme: " + why;
        return false;
      }
      out.dump.clear = theme == "clear";
      dump_only = true;
    } else {
      why = "not an option of grace-dev's: " + flag + " (--help lists them)";
      return false;
    }
  }
  if (out.mode == Cli::Mode::dump) {
    if (serving_only) {
      why = "--dump takes --size, --scale, --theme and --space-dir alone";
      return false;
    }
    if (!sized) {
      why = "--dump needs --size WxH";
      return false;
    }
  } else if (dump_only) {
    why = "--size, --scale and --theme are for --dump";
    return false;
  }
  return true;
}

}  // namespace grace::dev
