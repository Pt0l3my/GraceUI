// files.h: the web files grace-serve serves (§2.7, §7.2 items 15 and 21): the list in graceui.toml, the names a listed
// file may have, the content type its extension gives it, and files a host hands in from memory.
//
// graceui.toml is TOML; the server reads it strictly, as this subset (anything else fails the list, and the page loads
// then get the built-in notice page, item 15):
//   - lines of `key = value`, `[table]` headers, blank lines and `#` comments; a key bare (A-Z a-z 0-9 _ -) or a basic
//     string; a value a basic string with no escape, an integer of plain digits, true or false, an array of those on
//     one line, or an inline table of `key = value` pairs on one line; no key twice in a table;
//   - its `[files]` table names every file of GraceUI's folder with its size and SHA-256 (§2.7):
//       "web/index.html" = { size = 2311, sha256 = "<64 lowercase hex digits>" }
//     and the server serves only the entries under `web/`, each at its name without `web/` ("/index.html"), and
//     "web/index.html" at "/" too.
// A listed web file's name, after `web/`, is segments of A-Z a-z 0-9 and "-._~" joined by single '/', no segment
// starting with '.', at most 255 bytes, and never under `api/` (the server's own routes); its extension one of
// content_type_of's. Each is at most 16 MiB and all together at most 64 MiB (EST); a file that is missing, of another
// size or hash, or past these fails the whole list, so a damaged folder never serves half an app (§2.9's table).
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace grace::serve {

inline constexpr std::size_t kMaxManifest = 65536;            // graceui.toml itself
inline constexpr std::size_t kMaxListedFiles = 512;           // entries of [files]
inline constexpr std::size_t kMaxWebName = 255;
inline constexpr std::uint64_t kMaxWebFile = std::uint64_t{16} << 20;
inline constexpr std::uint64_t kMaxWebTotal = std::uint64_t{64} << 20;

struct ListedFile {
  std::string name;  // without "web/": "index.html", "assets/app.3f9c.js"
  std::uint64_t size = 0;
  std::array<std::uint8_t, 32> sha256{};
};

// graceui.toml's web files: true with `out` set, or false with `why`, a fixed text that names a line number at most,
// never the file's contents.
bool parse_manifest(std::string_view text, std::vector<ListedFile>& out, std::string& why) noexcept;

// A listed web file's name, as above (without "web/").
bool valid_web_name(std::string_view name) noexcept;

// The media type a web file is served with, by its extension: html, js, mjs, css, json, map, txt, svg, png, webp, ico,
// woff2. Empty for any other: such a file is not served.
std::string_view content_type_of(std::string_view name) noexcept;

// A file a host hands in from memory instead of a folder (grace-dev, a launcher with the web app compiled in, tests),
// named as served (without "web/"). The host vouches for its bytes: they are not checked against a list.
struct MemoryFile {
  std::string name;
  std::shared_ptr<const std::string> bytes;
};

}  // namespace grace::serve
