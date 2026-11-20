// abi.cpp: graceui.dll's exports (D73, invariant 15). No exception crosses any of them (D110): each answers through a
// catch-all, and everything they call in grace-host is noexcept besides. ABI 1's table is abi_table.cpp's.
//
// SAFETY: the raw pointers from callers here are the test exports' arguments, checked for null (and a struct's size
// against ours) and passed on with the capacity the caller gives; nothing else crosses the boundary.
#include "graceui.h"

#include "abi_table.h"
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
    // Every request, of any major, goes through the start (D71, D72): the first takes the mutex. Only the active copy
    // serves a table, and only ABI 1's.
    const grace::host::Copy copy = grace::host::request_start();
    return copy == grace::host::Copy::active && major == GRACEUI_ABI_MAJOR ? graceui::table1() : nullptr;
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

GRACEUI_API void GraceUI_Test_SetFirstRequestDelay(uint32_t ms) {
  try {
    grace::host::test_set_first_request_delay(ms);
  } catch (...) {
  }
}

GRACEUI_API void GraceUI_Test_TurnOffRender(void) {
  try {
    grace::host::test_turn_off_render();
  } catch (...) {
  }
}

GRACEUI_API uint64_t GraceUI_Test_GetPanelPublished(uint64_t panel) {
  try {
    return grace::host::test_panel_published(panel);
  } catch (...) {
    return 0;
  }
}

GRACEUI_API uint32_t GraceUI_Test_GetMenu(GraceUiTestMenu* out) {
  try {
    if (out == nullptr || out->size < sizeof(GraceUiTestMenu)) {
      return 0;
    }
    grace::host::test_fill_menu(*out);
    return 1;
  } catch (...) {
    return 0;
  }
}

GRACEUI_API uint32_t GraceUI_Test_GetServer(GraceUiTestServer* out) {
  try {
    if (out == nullptr || out->size < sizeof(GraceUiTestServer)) {
      return 0;
    }
    grace::host::test_fill_server(*out);
    return 1;
  } catch (...) {
    return 0;
  }
}

GRACEUI_API uint32_t GraceUI_Test_GetSetting(const char* key, char* out, uint32_t capacity) {
  try {
    return grace::host::test_copy_setting(key, out, capacity);
  } catch (...) {
    return 0;
  }
}

GRACEUI_API uint32_t GraceUI_Test_GetTopic(const char* topic, char* out, uint32_t capacity) {
  try {
    return grace::host::test_copy_topic(topic, out, capacity);
  } catch (...) {
    return 0;
  }
}

GRACEUI_API void GraceUI_Test_SetAtlasLimit(uint32_t side) {
  try {
    grace::host::test_set_atlas_limit(side);
  } catch (...) {
  }
}

GRACEUI_API void GraceUI_Test_SetCodePage(uint32_t code_page) {
  try {
    grace::host::test_set_code_page(code_page);
  } catch (...) {
  }
}

GRACEUI_API void GraceUI_Test_Pad(uint32_t user, uint32_t buttons) {
  try {
    grace::host::test_pad(user, static_cast<std::uint16_t>(buttons));
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

GRACEUI_API void GraceUI_Test_SetAntiCheatNames(const wchar_t* module_prefix, const wchar_t* parent_name) {
  try {
    grace::host::test_set_anti_cheat_names(module_prefix, parent_name);
  } catch (...) {
  }
}

GRACEUI_API uint32_t GraceUI_Test_GetNotice(uint32_t index, GraceUiTestNotice* out) {
  try {
    if (out == nullptr || out->size < sizeof(GraceUiTestNotice)) {
      return 0;
    }
    return grace::host::test_copy_notice(index, *out) ? 1 : 0;
  } catch (...) {
    return 0;
  }
}

GRACEUI_API void GraceUI_Test_ClearNotices(void) {
  try {
    grace::host::test_clear_notices();
  } catch (...) {
  }
}

GRACEUI_API uint32_t GraceUI_Test_GetConsumer(uint32_t index, GraceUiTestConsumer* out) {
  try {
    if (out == nullptr || out->size < sizeof(GraceUiTestConsumer)) {
      return 0;
    }
    return grace::host::test_copy_consumer(index, *out) ? 1 : 0;
  } catch (...) {
    return 0;
  }
}

#endif  // GRACEUI_TEST_EXPORTS

}  // extern "C"
