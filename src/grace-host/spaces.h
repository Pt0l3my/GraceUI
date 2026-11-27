// The spaces in the host (spaces.cpp; D169, §7.2 item 1): the mods' spaces registered across the ABI, GraceUI's own
// space (whose one settings store the in-game menu and the browser's views share), and the browser view's server, which
// GraceUI's own thread starts while a space asks for the browser view and dashboard.enabled is on, and stops otherwise;
// never in the notice-only mode (no socket beside an anti-cheat, D81).
#pragma once

#include <cstdint>
#include <string>
#include <vector>

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
// hotkeys.menu as the store holds it, applied at once (D208): after a change from either view.
void publish_menu_key(const grace::spaces::OwnSpace& own) noexcept;
// ui.scale, its default and Plain text, as the own space holds them, to the display that mods read (D180, D268).
void publish_ui_scale(const grace::spaces::OwnSpace& own) noexcept;

// GraceUI's own thread, at every wake: starts or stops the server as it is wanted, answers the browser's views on
// GraceUI's own space, and answers internal any mod's command left past its deadline.
void serve() noexcept;

// Whether GraceUI's own thread should wake by itself about once a second: while the server runs (the deadlines).
bool wants_timer() noexcept;

// An anti-cheat found after the first request: a world-state provider's flag, or GraceUI's own module re-check (every
// 60 s while the server runs). Any thread: the browser view's server stops at GraceUI's next wake and never starts
// again this session, whatever any space asks (§7.2 item 1, D81, invariant 4). One log line names `source`.
void close_for_anti_cheat(const char* source) noexcept;

// dashboard.disconnect, from the menu: a new session cookie, every browser connection closed (§7.2 item 17).
void disconnect_browsers() noexcept;

// ---- Other devices at home (G9, §7.2 item 16). GraceUI's own thread only.

// Pair a device, from the menu: a six-digit code the server takes for two minutes, once, and only while the sheet that
// shows it stays open (D113). Refused, with the reason in words, while Other devices is off, the browser view is not
// served, the PC has no home network address, or sixteen devices are paired.
struct PairingStart {
  bool ok = false;
  std::string words;
};
PairingStart start_pairing() noexcept;
// The sheet closed, or the menu with it: the code is void at once.
void cancel_pairing() noexcept;
// What the sheet shows now: the code with its time and the addresses to open, then the device paired, or why it ended.
struct PairingView {
  enum class State : std::uint8_t { none, showing, paired, ended };
  State state = State::none;
  std::string code;                    // six digits, while showing
  std::uint32_t seconds = 0;           // left, while showing
  std::vector<std::string> addresses;  // "http://ADDRESS:PORT/" for each the server listens on, while showing
  std::string paired_name;             // the name the device gave, once paired
  std::string why;                     // in words, once ended
};
PairingView pairing_view() noexcept;

#if defined(GRACEUI_TEST_EXPORTS)
struct TestView {
  std::uint32_t running;   // 1 while the server runs
  std::uint32_t port;      // its port, while it runs
  std::uint32_t spaces;    // mods' spaces registered
  std::uint32_t pending;   // commands polled and not yet answered
  std::uint32_t failed;    // 1 if it could not start this session
};
void test_view(TestView& out) noexcept;
// The module re-check's interval, for a test: 0 keeps 60 s.
void test_set_recheck_ms(std::uint32_t ms) noexcept;
// The code the pairing sheet shows (G9), as GraceUI_Test_GetPairingCode reads it: its length, 0 while none shows.
std::uint32_t test_pairing_code(char* out, std::uint32_t capacity) noexcept;
// The network listener's addresses for a test (127.0.0.2), taken at the server's next start.
void test_set_network_addresses(const std::uint8_t* addresses, std::uint32_t count) noexcept;
// One of GraceUI's own topics as JSON text (GraceUI_Test_GetTopic): its length, 0 for none or if it does not fit.
std::uint32_t test_topic(const char* topic, char* out, std::uint32_t capacity) noexcept;
#endif

}  // namespace grace::host::spaces
