// The spaces in the host (spaces.cpp; D169, §7.2 item 1): the mods' spaces registered across the ABI, GraceUI's own
// space (whose one settings store the in-game menu and the browser's views share), and the browser view's server, which
// GraceUI's own thread starts while a space asks for the browser view and dashboard.enabled is on, and stops otherwise;
// never in the notice-only mode (no socket beside an anti-cheat, D81).
#pragma once

#include <cstdint>
#include <string>

namespace grace::spaces {
class OwnSpace;
class Registry;
}  // namespace grace::spaces

namespace grace::host::spaces {

// GraceUI's version ("0.2.0") and its increment ("0.2"), as its own space names them to the views.
std::string version_text();
std::string increment_text();

// Any thread, once the session's stores are made (session::allocate): the mods' spaces.
grace::spaces::Registry& registry() noexcept;

// GraceUI's own thread only: GraceUI's own space, loaded at its first use; null if it does not load.
grace::spaces::OwnSpace* own() noexcept;
// Any thread: the same once GraceUI's own thread has loaded it, else null (it never loads it here). For tests to read.
grace::spaces::OwnSpace* loaded_own() noexcept;
// GraceUI's thread, after a change of its settings from either view: writes them to user\settings.json for the next
// session (D84, D179), once a revision.
void keep_own_settings() noexcept;
// ui.scale and its default, as the own space holds them, to the display's values (D180).
void publish_ui_scale(const grace::spaces::OwnSpace& own) noexcept;

// GraceUI's own thread, at every wake: starts or stops the server as it is wanted, answers the browser's views on
// GraceUI's own space, and answers internal any mod's command left past its deadline.
void serve() noexcept;

// Whether GraceUI's own thread should wake by itself about once a second: while the server runs (the deadlines).
bool wants_timer() noexcept;

// dashboard.disconnect, from the menu: a new session cookie, every browser connection closed (§7.2 item 17).
void disconnect_browsers() noexcept;

#if defined(GRACEUI_TEST_EXPORTS)
struct TestView {
  std::uint32_t running;   // 1 while the server runs
  std::uint32_t port;      // its port, while it runs
  std::uint32_t spaces;    // mods' spaces registered
  std::uint32_t pending;   // commands polled and not yet answered
  std::uint32_t failed;    // 1 if it could not start this session
};
void test_view(TestView& out) noexcept;
#endif

}  // namespace grace::host::spaces
