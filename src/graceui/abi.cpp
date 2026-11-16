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

GRACEUI_API uint32_t GraceUI_Test_GetHookName(uint32_t index, char* out, uint32_t capacity) {
  try {
    return out == nullptr ? 0 : grace::host::test_copy_hook_name(index, out, capacity);
  } catch (...) {
    return 0;
  }
}

GRACEUI_API uint32_t GraceUI_Test_RequestInput(uint32_t owner, uint32_t kind, uint32_t wants, GraceUiTestAnswer* out) {
  try {
    if (out == nullptr) {
      return 0;
    }
    grace::host::test_request_input(owner, kind, wants, *out);
    return 1;
  } catch (...) {
    return 0;
  }
}

GRACEUI_API uint32_t GraceUI_Test_ReleaseInput(uint32_t owner) {
  try {
    return grace::host::test_release_input(owner) ? 1 : 0;
  } catch (...) {
    return 0;
  }
}

GRACEUI_API void GraceUI_Test_SetWorld(uint32_t flags) {
  try {
    grace::host::test_set_world(flags);
  } catch (...) {
  }
}

GRACEUI_API void GraceUI_Test_SetCaret(int32_t x, int32_t y, int32_t height) {
  try {
    grace::host::test_set_caret(x, y, height);
  } catch (...) {
  }
}

GRACEUI_API void GraceUI_Test_SetCodePage(uint32_t code_page) {
  try {
    grace::host::test_set_code_page(code_page);
  } catch (...) {
  }
}

GRACEUI_API uint32_t GraceUI_Test_GetInput(GraceUiTestInput* out) {
  try {
    if (out == nullptr || out->size < sizeof(GraceUiTestInput)) {
      return 0;
    }
    grace::host::test_fill_input(*out);
    return 1;
  } catch (...) {
    return 0;
  }
}

GRACEUI_API uint32_t GraceUI_Test_GetChars(uint32_t* out, uint32_t capacity) {
  try {
    return out == nullptr ? 0 : grace::host::test_copy_chars(out, capacity);
  } catch (...) {
    return 0;
  }
}

GRACEUI_API uint32_t GraceUI_Test_GetInputCosts(uint32_t kind, uint32_t* out, uint32_t capacity) {
  try {
    return out == nullptr ? 0 : grace::host::test_copy_input_costs(kind, out, capacity);
  } catch (...) {
    return 0;
  }
}

#endif  // GRACEUI_TEST_EXPORTS

}  // extern "C"
