// abi.cpp: graceui.dll's exports (D73, invariant 15). No exception crosses any of them (D110): each answers through a
// catch-all, and everything they call in grace-host is noexcept besides.
//
// SAFETY: the raw pointers from callers here are the test exports' arguments, checked for null and passed on with the
// capacity the caller gives; nothing else crosses the boundary.
#include "graceui.h"

#include "grace/host/host.h"

#if defined(GRACEUI_TEST_EXPORTS)
#include "graceui_test.h"
#endif

extern "C" {

GRACEUI_API uint32_t GraceUI_Version(void) {
  return GRACEUI_VERSION_PACK(GRACEUI_BUILD_MAJOR, GRACEUI_BUILD_MINOR, GRACEUI_BUILD_PATCH);
}

GRACEUI_API const void* GraceUI_RequestApi(uint32_t major) {
  try {
    (void)grace::host::request_start();
    // ABI 1's table, GraceUiApi1, comes with roadmap item S10.4.b; until then every major is answered with null.
    (void)major;
    return nullptr;
  } catch (...) {
    return nullptr;
  }
}

#if defined(GRACEUI_TEST_EXPORTS)

GRACEUI_API void GraceUI_Test_SetPanel(uint32_t on) {
  try {
    grace::host::test_set_panel(on != 0);
  } catch (...) {
  }
}

GRACEUI_API uint32_t GraceUI_Test_GetState(GraceUiTestState* out) {
  try {
    if (out == nullptr || out->size < sizeof(GraceUiTestState)) {
      return 0;
    }
    grace::host::test_fill_state(*out);
    return 1;
  } catch (...) {
    return 0;
  }
}

GRACEUI_API void GraceUI_Test_ResetCounters(void) {
  try {
    grace::host::test_reset_counters();
  } catch (...) {
  }
}

GRACEUI_API uint32_t GraceUI_Test_GetFrameCosts(GraceUiTestFrameCost* out, uint32_t capacity) {
  try {
    return out == nullptr ? 0 : grace::host::test_copy_frame_costs(out, capacity);
  } catch (...) {
    return 0;
  }
}

GRACEUI_API uint32_t GraceUI_Test_GetLogLine(uint32_t index, char* out, uint32_t capacity) {
  try {
    return out == nullptr ? 0 : grace::host::test_copy_log_line(index, out, capacity);
  } catch (...) {
    return 0;
  }
}

#endif  // GRACEUI_TEST_EXPORTS

}  // extern "C"
