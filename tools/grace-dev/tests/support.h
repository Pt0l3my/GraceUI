// support.h: what grace-dev's test files share: the tests' own folder, a temporary folder, and a file's text.
#pragma once

#include <filesystem>
#include <fstream>
#include <iterator>
#include <random>
#include <string>
#include <string_view>

#include "json_text.h"

namespace grace::dev::test {

inline std::filesystem::path tests_folder() { return path_from_utf8(GRACE_DEV_TESTS); }

inline std::string read_text(const std::filesystem::path& path) {
  std::ifstream in(path, std::ios::binary);
  return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

inline bool contains(std::string_view text, std::string_view part) { return text.find(part) != std::string_view::npos; }

// A folder of its own under the system's temporary folder, removed afterwards.
struct TempFolder {
  TempFolder() {
    std::random_device random;
    path = std::filesystem::temp_directory_path() /
           ("grace-dev-test-" + std::to_string(random()) + "-" + std::to_string(random()));
    std::filesystem::create_directories(path);
  }
  ~TempFolder() {
    std::error_code error;
    std::filesystem::remove_all(path, error);
  }
  TempFolder(const TempFolder&) = delete;
  TempFolder& operator=(const TempFolder&) = delete;

  void write(const std::string& name, std::string_view text) const {
    std::filesystem::create_directories((path / name).parent_path());
    std::ofstream out(path / name, std::ios::binary);
    out.write(text.data(), static_cast<std::streamsize>(text.size()));
  }
  void copy_from(const std::filesystem::path& from) const {
    for (const auto& entry : std::filesystem::directory_iterator(from)) {
      std::filesystem::copy_file(entry.path(), path / entry.path().filename(),
                                 std::filesystem::copy_options::overwrite_existing);
    }
  }
  std::filesystem::path path;
};

}  // namespace grace::dev::test
