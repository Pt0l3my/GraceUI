// files.cpp: graceui.toml's list of web files, read strictly (files.h), and the worker's reading and checking of them
// at start (§2.7, §7.2 items 15 and 21, §7.6). Nothing here is on the I/O thread.
#include <algorithm>
#include <fstream>
#include <set>
#include <system_error>

#include "grace/serve/codec.h"
#include "grace/serve/files.h"
#include "internal.h"

namespace grace::serve {
namespace {

bool is_alnum(char c) noexcept { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9'); }

bool is_bare_key_char(char c) noexcept { return is_alnum(c) || c == '_' || c == '-'; }

// A value of the subset files.h describes.
struct Entry;
struct Value {
  enum class Kind : std::uint8_t { string, integer, boolean, array, table };
  Kind kind = Kind::string;
  std::string text;
  std::uint64_t number = 0;
  std::vector<Entry> table;
};
struct Entry {
  std::string name;
  Value value;
};

class LineReader {
 public:
  explicit LineReader(std::string_view line) noexcept : line_(line) {}

  void skip_space() noexcept {
    while (at_ < line_.size() && (line_[at_] == ' ' || line_[at_] == '\t')) {
      ++at_;
    }
  }
  bool at_end() const noexcept { return at_ >= line_.size(); }
  char peek() const noexcept { return at_ < line_.size() ? line_[at_] : '\0'; }
  bool take(char c) noexcept {
    if (peek() != c || at_end()) {
      return false;
    }
    ++at_;
    return true;
  }
  // Nothing but whitespace and a comment is left.
  bool rest_is_blank() noexcept {
    skip_space();
    return at_end() || peek() == '#';
  }

  bool key(std::string& out) noexcept {
    out.clear();
    if (peek() == '"') {
      return basic_string(out) && !out.empty();
    }
    while (!at_end() && is_bare_key_char(peek())) {
      out.push_back(line_[at_++]);
    }
    return !out.empty();
  }

  // A basic string with no escape and no control character.
  bool basic_string(std::string& out) noexcept {
    out.clear();
    if (!take('"')) {
      return false;
    }
    while (!at_end()) {
      const char c = line_[at_++];
      if (c == '"') {
        return true;
      }
      if (c == '\\' || (static_cast<unsigned char>(c) < 0x20 && c != '\t') || c == 0x7F) {
        return false;
      }
      out.push_back(c);
    }
    return false;
  }

  bool integer(std::uint64_t& out) noexcept {
    out = 0;
    const std::size_t start = at_;
    while (!at_end() && peek() >= '0' && peek() <= '9') {
      const auto digit = static_cast<std::uint64_t>(line_[at_++] - '0');
      if (out > (UINT64_MAX - digit) / 10) {
        return false;
      }
      out = out * 10 + digit;
    }
    const std::size_t digits = at_ - start;
    // TOML: no leading zero.
    return digits != 0 && !(digits > 1 && line_[start] == '0');
  }

  bool word(std::string_view expected) noexcept {
    if (line_.substr(at_, expected.size()) != expected) {
      return false;
    }
    at_ += expected.size();
    return true;
  }

  // A scalar, an array of scalars, or (when `nested` allows) an inline table of scalars.
  bool value(Value& out, bool nested) noexcept {
    const char c = peek();
    if (c == '"') {
      out.kind = Value::Kind::string;
      return basic_string(out.text);
    }
    if (c >= '0' && c <= '9') {
      out.kind = Value::Kind::integer;
      return integer(out.number);
    }
    if (c == 't' || c == 'f') {
      out.kind = Value::Kind::boolean;
      out.number = c == 't' ? 1 : 0;
      return word(c == 't' ? "true" : "false");
    }
    if (c == '[' && nested) {
      out.kind = Value::Kind::array;
      take('[');
      skip_space();
      if (take(']')) {
        return true;
      }
      while (true) {
        Value item;
        skip_space();
        if (peek() == ']') {  // a trailing comma
          take(']');
          return true;
        }
        if (!value(item, false)) {
          return false;
        }
        skip_space();
        if (take(']')) {
          return true;
        }
        if (!take(',')) {
          return false;
        }
      }
    }
    if (c == '{' && nested) {
      out.kind = Value::Kind::table;
      take('{');
      skip_space();
      if (take('}')) {
        return true;
      }
      std::set<std::string> keys;
      while (true) {
        skip_space();
        std::string name;
        if (!key(name) || !keys.insert(name).second) {
          return false;
        }
        skip_space();
        if (!take('=')) {
          return false;
        }
        skip_space();
        Value item;
        if (!value(item, false)) {
          return false;
        }
        out.table.push_back(Entry{std::move(name), std::move(item)});
        skip_space();
        if (take('}')) {
          return true;
        }
        if (!take(',')) {
          return false;
        }
      }
    }
    return false;
  }

 private:
  std::string_view line_;
  std::size_t at_ = 0;
};

bool hex_digest(std::string_view text, std::array<std::uint8_t, 32>& out) noexcept {
  if (text.size() != 64) {
    return false;
  }
  for (std::size_t i = 0; i < 32; ++i) {
    std::uint8_t byte = 0;
    for (std::size_t j = 0; j < 2; ++j) {
      const char c = text[2 * i + j];
      std::uint8_t nibble = 0;
      if (c >= '0' && c <= '9') {
        nibble = static_cast<std::uint8_t>(c - '0');
      } else if (c >= 'a' && c <= 'f') {
        nibble = static_cast<std::uint8_t>(c - 'a' + 10);
      } else {
        return false;  // lowercase only, as the release job writes it
      }
      byte = static_cast<std::uint8_t>((byte << 4) | nibble);
    }
    out[i] = byte;
  }
  return true;
}

bool refuse(std::string& why, std::size_t line, const char* reason) {
  why = "graceui.toml, line " + std::to_string(line) + ": " + reason;
  return false;
}

}  // namespace

bool valid_web_name(std::string_view name) noexcept {
  if (name.empty() || name.size() > kMaxWebName || name == "api" || name.starts_with("api/")) {
    return false;
  }
  bool segment_start = true;
  for (const char c : name) {
    if (c == '/') {
      if (segment_start) {
        return false;  // an empty segment
      }
      segment_start = true;
      continue;
    }
    if (segment_start && c == '.') {
      return false;  // ".", "..", or a hidden name
    }
    if (!is_alnum(c) && c != '-' && c != '.' && c != '_' && c != '~') {
      return false;
    }
    segment_start = false;
  }
  return !segment_start;
}

std::string_view content_type_of(std::string_view name) noexcept {
  const std::size_t slash = name.rfind('/');
  const std::string_view last = slash == std::string_view::npos ? name : name.substr(slash + 1);
  const std::size_t dot = last.rfind('.');
  if (dot == std::string_view::npos || dot == 0) {
    return {};
  }
  const std::string_view extension = last.substr(dot + 1);
  struct Type {
    std::string_view extension;
    std::string_view type;
  };
  static constexpr Type kTypes[] = {
      {"html", "text/html; charset=utf-8"},  {"js", "text/javascript; charset=utf-8"},
      {"mjs", "text/javascript; charset=utf-8"}, {"css", "text/css; charset=utf-8"},
      {"json", "application/json"},           {"map", "application/json"},
      {"txt", "text/plain; charset=utf-8"},  {"svg", "image/svg+xml"},
      {"png", "image/png"},                  {"webp", "image/webp"},
      {"ico", "image/vnd.microsoft.icon"},   {"woff2", "font/woff2"},
  };
  for (const Type& type : kTypes) {
    if (type.extension == extension) {
      return type.type;
    }
  }
  return {};
}

namespace {

bool parse_lines(std::string_view text, std::vector<ListedFile>& out, std::string& why) {
  {
    if (text.size() > kMaxManifest) {
      return refuse(why, 0, "longer than the server reads");
    }
    if (!valid_utf8(text)) {
      return refuse(why, 0, "not UTF-8");
    }
    std::set<std::string> tables;
    std::set<std::string> keys;  // of the current table
    std::string table;
    std::size_t entries = 0;
    std::uint64_t total = 0;
    std::size_t number = 0;
    std::size_t start = 0;
    while (start <= text.size()) {
      const std::size_t end = std::min(text.find('\n', start), text.size());
      std::string_view line = text.substr(start, end - start);
      start = end + 1;
      ++number;
      if (!line.empty() && line.back() == '\r') {
        line.remove_suffix(1);
      }
      if (line.find('\r') != std::string_view::npos) {
        return refuse(why, number, "a bare carriage return");
      }
      LineReader reader(line);
      if (reader.rest_is_blank()) {
        continue;
      }
      if (reader.take('[')) {
        reader.skip_space();
        std::string name;
        if (reader.peek() == '[' || !reader.key(name)) {
          return refuse(why, number, "a table header the server does not read");
        }
        reader.skip_space();
        if (!reader.take(']') || !reader.rest_is_blank()) {
          return refuse(why, number, "a table header the server does not read");
        }
        if (keys.contains(name) && table.empty()) {
          return refuse(why, number, "a table named as a key");
        }
        if (!tables.insert(name).second) {
          return refuse(why, number, "a table defined twice");
        }
        table = std::move(name);
        keys.clear();
        continue;
      }
      std::string key;
      if (!reader.key(key)) {
        return refuse(why, number, "not a key the server reads");
      }
      reader.skip_space();
      if (!reader.take('=')) {
        return refuse(why, number, "not a key and its value");
      }
      reader.skip_space();
      Value value;
      if (!reader.value(value, true) || !reader.rest_is_blank()) {
        return refuse(why, number, "a value the server does not read");
      }
      if (!keys.insert(key).second || (table.empty() && tables.contains(key))) {
        return refuse(why, number, "a key given twice");
      }
      if (table != "files") {
        continue;
      }
      if (++entries > kMaxListedFiles) {
        return refuse(why, number, "more files than the server takes");
      }
      if (value.kind != Value::Kind::table || value.table.size() != 2) {
        return refuse(why, number, "a file without exactly its size and sha256");
      }
      ListedFile file;
      bool has_size = false;
      bool has_hash = false;
      for (const Entry& entry : value.table) {
        const Value& item = entry.value;
        if (entry.name == "size" && item.kind == Value::Kind::integer) {
          file.size = item.number;
          has_size = true;
        } else if (entry.name == "sha256" && item.kind == Value::Kind::string && hex_digest(item.text, file.sha256)) {
          has_hash = true;
        }
      }
      if (!has_size || !has_hash) {
        return refuse(why, number, "a file without exactly its size and sha256");
      }
      if (!key.starts_with("web/")) {
        continue;  // graceui.dll, the fonts and the notices: checked by others, never served
      }
      file.name = key.substr(4);
      if (!valid_web_name(file.name) || content_type_of(file.name).empty()) {
        return refuse(why, number, "a web file's name or type the server does not serve");
      }
      if (file.size > kMaxWebFile || (total += file.size) > kMaxWebTotal) {
        return refuse(why, number, "larger web files than the server takes");
      }
      out.push_back(std::move(file));
    }
    return true;
  }
}

}  // namespace

bool parse_manifest(std::string_view text, std::vector<ListedFile>& out, std::string& why) noexcept {
  try {
    out.clear();
    why.clear();
    if (parse_lines(text, out, why)) {
      return true;
    }
  } catch (...) {
    why = "graceui.toml: could not be read";
  }
  out.clear();  // a list that fails serves nothing of itself
  return false;
}

namespace detail {
namespace {

// Reads exactly `size` bytes of a regular file inside the web folder; anything else (a link, a folder, another size) is
// a failure. Only listed names reach here, so no path is ever made from a request.
bool read_exact(const std::filesystem::path& path, std::uint64_t size, std::string& out) {
  std::error_code error;
  const std::filesystem::file_status status = std::filesystem::symlink_status(path, error);
  if (error || !std::filesystem::is_regular_file(status)) {
    return false;
  }
  if (std::filesystem::file_size(path, error) != size || error) {
    return false;
  }
  std::ifstream in(path, std::ios::binary);
  if (!in) {
    return false;
  }
  out.assign(static_cast<std::size_t>(size), '\0');
  in.read(out.data(), static_cast<std::streamsize>(size));
  return static_cast<std::uint64_t>(in.gcount()) == size && in.peek() == std::char_traits<char>::eof();
}

std::shared_ptr<const FileTable> failed(std::string failure) {
  auto table = std::make_shared<FileTable>();
  table->ok = false;
  table->failure = std::move(failure);
  return table;
}

}  // namespace

const WebFile* FileTable::find(std::string_view path) const noexcept {
  if (path.empty() || path.front() != '/') {
    return nullptr;
  }
  const std::string_view name = path == "/" ? std::string_view("index.html") : path.substr(1);
  for (const WebFile& file : files) {
    if (file.name == name) {
      return &file;
    }
  }
  return nullptr;
}

std::shared_ptr<const FileTable> load_files(const ServerConfig& config) noexcept {
  try {
    auto table = std::make_shared<FileTable>();
    if (config.from_memory) {
      std::uint64_t total = 0;
      for (const MemoryFile& file : config.memory_files) {
        const std::string_view type = content_type_of(file.name);
        if (!valid_web_name(file.name) || type.empty() || file.bytes == nullptr ||
            file.bytes->size() > kMaxWebFile || (total += file.bytes->size()) > kMaxWebTotal ||
            table->find("/" + file.name) != nullptr) {
          return failed("a web file handed in has a name, type or size the server does not serve");
        }
        table->files.push_back(WebFile{file.name, type, type.starts_with("text/html"), file.bytes});
      }
      table->ok = true;
      return table;
    }
    const std::filesystem::path manifest_path = config.folder / "graceui.toml";
    std::string manifest;
    std::error_code error;
    const std::uintmax_t manifest_size = std::filesystem::file_size(manifest_path, error);
    if (error || manifest_size > kMaxManifest || !read_exact(manifest_path, manifest_size, manifest)) {
      return failed("graceui.toml is missing or unreadable");
    }
    std::vector<ListedFile> listed;
    std::string why;
    if (!parse_manifest(manifest, listed, why)) {
      return failed(why);
    }
    const std::filesystem::path web = config.folder / "web";
    if (!std::filesystem::is_directory(web, error) || error) {
      return failed("the web folder is missing");
    }
    for (const ListedFile& file : listed) {
      auto bytes = std::make_shared<std::string>();
      if (!read_exact(web / std::filesystem::path(file.name), file.size, *bytes)) {
        return failed("web/" + file.name + ": missing, or not of its listed size");
      }
      Sha256 sha;
      sha.update(*bytes);
      if (sha.finish() != file.sha256) {
        return failed("web/" + file.name + ": its SHA-256 differs from graceui.toml");
      }
      const std::string_view type = content_type_of(file.name);
      table->files.push_back(WebFile{file.name, type, type.starts_with("text/html"), std::move(bytes)});
    }
    table->ok = true;
    return table;
  } catch (...) {
    return failed("the web files could not be read");
  }
}

std::string_view notice_page() noexcept {
  return "<!DOCTYPE html>\n"
         "<html lang=\"en-GB\"><head><meta charset=\"utf-8\"><title>GraceUI</title></head><body>\n"
         "<h1>The browser view is unavailable</h1>\n"
         "<p>GraceUI could not read or check its web files, so the browser view cannot open this session. The game "
         "is not affected.</p>\n"
         "<p>To mend it, close the game and install GraceUI again, so that its folder is whole. GraceUI's log names "
         "the file that failed.</p>\n"
         "</body></html>\n";
}

}  // namespace detail
}  // namespace grace::serve
