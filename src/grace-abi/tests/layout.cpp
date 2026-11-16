// SPDX-License-Identifier: MIT
// layout.cpp: graceui.h compiled as C++ (D109), with -Wold-style-cast as GraceUI's own code is: the layout's checks are
// compile-time (layout_checks.h); main() checks GRACEUI_API1_COVERS as a C++ compiler sees it.
#include <cstdint>

#include "layout_checks.h"

int main() {
  GraceUiApi1 table{};
  table.size = static_cast<std::uint32_t>(sizeof table);
  if (!GRACEUI_API1_COVERS(&table, has_cap) || !GRACEUI_API1_COVERS(&table, log)) {
    return 1;
  }
  table.size = static_cast<std::uint32_t>(offsetof(GraceUiApi1, log));  // a table one slot short
  if (GRACEUI_API1_COVERS(&table, log) || !GRACEUI_API1_COVERS(&table, set_world_state)) {
    return 2;
  }
  const GraceUiApi1* none = nullptr;
  if (GRACEUI_API1_COVERS(none, connect)) {
    return 3;
  }
  return 0;
}
