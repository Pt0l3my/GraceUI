// SPDX-License-Identifier: MIT
// graceui_sys.hpp: a thin C++ wrapper over graceui_sys.h and ABI 1's table (D70, D136). Header-only, C++17 or later:
// every call checks that the table covers its slot (an older GraceUI's table may be shorter, D74) and answers
// GRACEUI_NOT_YET where it does not; strings are std::string_view; nothing allocates and nothing throws.
#pragma once

#include <cstdint>
#include <string_view>

#include "graceui_sys.h"

namespace graceui {

// A mod as connect names it (GraceUiConnectRequest).
struct Mod {
  std::string_view id;    // 3 to 32 of a-z, 0-9 and '-'
  std::string_view name;  // what players see
  std::uint32_t version = 0;
  std::uint32_t floor = 0;  // the oldest GraceUI it works with
  std::string_view caps;    // GRACEUI_CAP_* names separated by spaces
};

class Api {
 public:
  constexpr Api() noexcept = default;
  constexpr explicit Api(const GraceUiApi1* table) noexcept : table_(table) {}

  // Finds GraceUI as graceui_sys_find does: from the mod's own thread, never from DllMain.
  static Api find(std::uint32_t flags = GRACEUI_SYS_LOAD, std::uint32_t timeout_ms = GRACEUI_SYS_TIMEOUT_MS,
                  GraceUiSysFound* found = nullptr) noexcept {
    return Api(graceui_sys_find(flags, timeout_ms, found));
  }

  constexpr explicit operator bool() const noexcept { return table_ != nullptr; }
  constexpr const GraceUiApi1* table() const noexcept { return table_; }
  std::uint32_t version() const noexcept { return table_ != nullptr ? table_->graceui_version : 0; }

  bool has_cap(std::string_view cap) const noexcept {
    return GRACEUI_API1_COVERS(table_, has_cap) && table_->has_cap(cap.data(), cap.size()) != 0;
  }

  GraceUiStatus connect(const Mod& mod, GraceUiConnectAnswer& answer) const noexcept {
    answer = GraceUiConnectAnswer{};
    answer.size = sizeof answer;
    if (!GRACEUI_API1_COVERS(table_, connect)) {
      answer.status = GRACEUI_NOT_YET;
      return GRACEUI_NOT_YET;
    }
    GraceUiConnectRequest request{};
    request.size = sizeof request;
    request.abi_minor = GRACEUI_ABI1_MINOR;
    request.version = mod.version;
    request.floor = mod.floor;
    request.id = mod.id.data();
    request.id_length = mod.id.size();
    request.name = mod.name.data();
    request.name_length = mod.name.size();
    request.caps = mod.caps.data();
    request.caps_length = mod.caps.size();
    return table_->connect(&request, &answer);
  }

  GraceUiStatus disconnect(GraceUiConsumer consumer) const noexcept {
    return GRACEUI_API1_COVERS(table_, disconnect) ? table_->disconnect(consumer) : GRACEUI_NOT_YET;
  }

  // A notice: `key` 0 for a new one, else the mod's key, which replaces the notice showing under it, and withdraws it
  // with an empty text; `duration_ms` 0 until replaced or withdrawn.
  GraceUiStatus notice(GraceUiConsumer consumer, std::string_view text, std::uint32_t tone = GRACEUI_TONE_NORMAL,
                       std::uint32_t key = 0, std::uint32_t duration_ms = 0) const noexcept {
    if (!GRACEUI_API1_COVERS(table_, notice)) {
      return GRACEUI_NOT_YET;
    }
    GraceUiNotice in{};
    in.size = sizeof in;
    in.tone = tone;
    in.key = key;
    in.duration_ms = duration_ms;
    in.text = text.data();
    in.text_length = text.size();
    return table_->notice(consumer, &in);
  }

 private:
  const GraceUiApi1* table_ = nullptr;
};

}  // namespace graceui
