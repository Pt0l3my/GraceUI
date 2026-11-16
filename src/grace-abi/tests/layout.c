/* SPDX-License-Identifier: MIT */
/* layout.c: graceui.h compiled as C (D109). The layout's checks are compile-time (layout_checks.h); main() checks the
 * size rule a mod follows, GRACEUI_API1_COVERS, as a C compiler sees it. */
#include <string.h>

#include "layout_checks.h"

int main(void) {
  GraceUiApi1 table;
  const GraceUiApi1* none = NULL;
  GraceUiConnectRequest request = {0};
  memset(&table, 0, sizeof table);
  table.size = (uint32_t)sizeof table;
  if (!GRACEUI_API1_COVERS(&table, has_cap) || !GRACEUI_API1_COVERS(&table, log)) {
    return 1;
  }
  table.size = (uint32_t)offsetof(GraceUiApi1, log); /* a table one slot short */
  if (GRACEUI_API1_COVERS(&table, log) || !GRACEUI_API1_COVERS(&table, set_world_state)) {
    return 2;
  }
  if (GRACEUI_API1_COVERS(none, connect)) {
    return 3;
  }
  request.size = (uint32_t)sizeof request;
  return request.size == 64u ? 0 : 4;
}
