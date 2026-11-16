// anticheat.h: the names of GraceUI's anti-cheat check (D88, D133; EldenNet ARCH §5.3, the same two clauses): a module
// of the process whose file name starts with "EasyAntiCheat", or a parent process whose image is
// "start_protected_game.exe", each compared without regard to the case of ASCII letters. grace-host gathers the names
// (anticheat.cpp); this decides. Pure, so that the matching is tested on Linux; a test build of GraceUI may compare
// other names, so that the harness simulates a hit with no anti-cheat software present.
#pragma once

#include <string_view>

namespace grace::abi {

inline constexpr std::u16string_view kAntiCheatModulePrefix = u"EasyAntiCheat";
inline constexpr std::u16string_view kAntiCheatParent = u"start_protected_game.exe";

constexpr char16_t ascii_lower(char16_t c) noexcept {
  return c >= u'A' && c <= u'Z' ? static_cast<char16_t>(c - u'A' + u'a') : c;
}

// `name` starts with `prefix`, ASCII letters compared without their case. An empty prefix matches nothing.
constexpr bool starts_with_folded(std::u16string_view name, std::u16string_view prefix) noexcept {
  if (prefix.empty() || name.size() < prefix.size()) {
    return false;
  }
  for (std::size_t i = 0; i < prefix.size(); ++i) {
    if (ascii_lower(name[i]) != ascii_lower(prefix[i])) {
      return false;
    }
  }
  return true;
}

// `name` is `other`, ASCII letters compared without their case. An empty name matches nothing.
constexpr bool equals_folded(std::u16string_view name, std::u16string_view other) noexcept {
  return !name.empty() && name.size() == other.size() && starts_with_folded(name, other);
}

// The module clause: a loaded module's file name (no folder).
constexpr bool anti_cheat_module(std::u16string_view file_name,
                                 std::u16string_view prefix = kAntiCheatModulePrefix) noexcept {
  return starts_with_folded(file_name, prefix);
}

// The parent clause: the parent process's image name (no folder).
constexpr bool anti_cheat_parent(std::u16string_view image_name,
                                 std::u16string_view parent = kAntiCheatParent) noexcept {
  return equals_folded(image_name, parent);
}

}  // namespace grace::abi
