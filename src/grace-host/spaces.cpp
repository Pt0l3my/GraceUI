// spaces.cpp: the spaces in the host, and the browser view's server (spaces.h; D169, D161).
#include "spaces.h"

#include <windows.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "anticheat.h"
#include "display.h"
#include "draw_atlas.h"
#include "grace/abi/spin.h"
#include "grace/serve/server.h"
#include "grace/spaces/own_space.h"
#include "grace/spaces/registry.h"
#include "hooks.h"
#include "input.h"
#include "log.h"
#include "session.h"

namespace grace::host::spaces {
namespace {

// GraceUI's own thread's: made at its first serve(), never destroyed (GraceUI never unloads).
constinit std::atomic<grace::spaces::OwnSpace*> g_own{nullptr};  // published once, by GraceUI's own thread
constinit bool g_own_failed = false;
constinit grace::serve::Server* g_server = nullptr;
constinit std::atomic<bool> g_start_failed{false};
constinit std::uint64_t g_kept_revision = 1;  // the own space's revision last written to user\settings.json (GraceUI's thread)
constinit std::uint64_t g_kept_quiet = 1;     // the quiet mods' revision last written there (D181)
constinit bool g_keep_failed = false;
constinit std::atomic<bool> g_running{false};
constinit std::atomic<std::uint32_t> g_port{0};
// An anti-cheat found after the first request (§7.2 item 1, D81): the server is closed for the rest of the session.
constinit std::atomic<bool> g_closed{false};
constinit std::int64_t g_next_recheck_ms = 0;  // GraceUI's thread: the next module re-check, while the server runs
constexpr std::int64_t kRecheckMs = 60'000;
constinit std::atomic<std::uint32_t> g_test_recheck_ms{0};

std::int64_t steady_ms() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch())
      .count();
}

std::int64_t recheck_interval_ms() {
  const std::uint32_t test = g_test_recheck_ms.load(std::memory_order_relaxed);
  return test != 0 ? static_cast<std::int64_t>(test) : kRecheckMs;
}

void server_log(grace::serve::LogLevel, std::string_view line) {
  log_line("browser view: %.*s", static_cast<int>(line.size()), line.data());
}

bool wanted(const grace::spaces::OwnSpace& own) {
  const protocol::Json& enabled = own.store().held("dashboard.enabled");
  return !session::notice_only() && !g_closed.load(std::memory_order_acquire) && registry().wants_browser() &&
         enabled.is_boolean() && enabled.get<bool>();
}

void start(const grace::spaces::OwnSpace& own) {
  std::wstring folder;
  if (!atlas::own_folder(folder)) {
    g_start_failed.store(true, std::memory_order_relaxed);
    log_line("the browser view cannot start: GraceUI's own folder could not be named");
    return;
  }
  const protocol::Json& port = own.store().held("dashboard.port");
  grace::serve::ServerConfig config;
  config.port = static_cast<std::uint16_t>(port.is_number_integer() ? port.get<std::int64_t>() : 8642);
  config.graceui_version = version_text();
  config.folder = std::filesystem::path(folder);
  config.log = &server_log;
  const grace::serve::StartResult result = g_server->start(config);
  if (result != grace::serve::StartResult::ok) {
    g_start_failed.store(true, std::memory_order_relaxed);  // not tried again this session: the port's setting takes effect at the next launch
    const std::string_view why = grace::serve::to_string(result);
    log_line("the browser view could not start: %.*s", static_cast<int>(why.size()), why.data());
    session::own_notice(0, GRACEUI_TONE_WARNING, "The browser view could not start on port %u: %s.",
                        static_cast<unsigned>(config.port),
                        result == grace::serve::StartResult::port_taken ? "another program holds it" : "see the log");
    return;
  }
  g_port.store(config.port, std::memory_order_relaxed);
  g_running.store(true, std::memory_order_release);
  g_next_recheck_ms = steady_ms() + recheck_interval_ms();
  log_line("the browser view is served on loopback, port %u", static_cast<unsigned>(config.port));
}

}  // namespace

std::string version_text() {
  return std::to_string(GRACEUI_BUILD_MAJOR) + "." + std::to_string(GRACEUI_BUILD_MINOR) + "." +
         std::to_string(GRACEUI_BUILD_PATCH);
}

std::string increment_text() {
  return std::to_string(GRACEUI_BUILD_MAJOR) + "." + std::to_string(GRACEUI_BUILD_MINOR);
}

grace::spaces::Registry& registry() noexcept {
  static grace::spaces::Registry* const made = [] {
    auto* made = new grace::spaces::Registry([](std::string_view line) {
      log_line("%.*s", static_cast<int>(line.size()), line.data());
    });
    made->set_game_wake([] { hooks::wake(); });  // the menu's: a space came or went, a state or a result came back
    return made;
  }();
  return *made;
}

namespace {

// GraceUI's settings kept between sessions (D84, D179): Game\GraceUI\user\settings.json, beside graceui.dll, outside
// any release. Read and written on GraceUI's own thread only; a line of the log says what happened, never a value.
constexpr std::uintmax_t kSettingsFileMax = 64 * 1024;

bool settings_path(std::filesystem::path& out) {
  std::wstring folder;
  if (!atlas::own_folder(folder)) {
    return false;
  }
  out = std::filesystem::path(folder) / L"user" / L"settings.json";
  return true;
}

void restore_saved(grace::spaces::OwnSpace& own) {
  std::filesystem::path path;
  std::error_code error;
  if (!settings_path(path) || !std::filesystem::is_regular_file(path, error)) {
    return;  // none yet: the defaults
  }
  const std::uintmax_t size = std::filesystem::file_size(path, error);
  if (error || size > kSettingsFileMax) {
    log_line("GraceUI's settings were not read: the file is %s", error ? "unreadable" : "too large");
    return;
  }
  std::ifstream in(path, std::ios::binary);
  const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  const protocol::Json file = protocol::Json::parse(text, nullptr, false);
  if (file.is_discarded() || !file.is_object() || !file.contains("values")) {
    log_line("GraceUI's settings were not read: the file is not GraceUI's settings; the defaults stand");
    return;
  }
  const grace::pages::SettingsStore::Restored back = own.restore_settings(file["values"]);
  log_line("GraceUI's settings: %u kept from an earlier session, %u left out", static_cast<unsigned>(back.taken),
           static_cast<unsigned>(back.left_out));
  // The mods whose notices the player quieted (D181): ids only, each as connect would take it.
  if (const auto found = file.find("quiet_mods"); found != file.end() && found->is_array()) {
    std::vector<std::string> ids;
    for (const protocol::Json& id : *found) {
      if (id.is_string() && ids.size() < grace::abi::kQuietMods) {
        ids.push_back(id.get<std::string>());
      }
    }
    const std::size_t taken = session::quiet().restore(ids);
    log_line("GraceUI's settings: %u mods' notices kept quiet", static_cast<unsigned>(taken));
  }
  g_kept_quiet = session::quiet().ids_revision();
}

// What a mod's caps say it uses, in the Mods page's words; a name GraceUI does not know is left out.
std::string uses_words(std::string_view caps) {
  static constexpr std::pair<std::string_view, std::string_view> kWords[] = {
      {GRACEUI_CAP_PAGES, "pages"},         {GRACEUI_CAP_NOTICES, "notices"},
      {GRACEUI_CAP_PANELS, "panels"},       {GRACEUI_CAP_TEXTURES, "textures"},
      {GRACEUI_CAP_FACES, "faces"},         {GRACEUI_CAP_INPUT, "input"},
      {GRACEUI_CAP_HOLDER_EVENTS, "input events"}, {GRACEUI_CAP_HOTKEYS, "hotkeys"},
      {GRACEUI_CAP_WORLD_STATE, "world state"},    {GRACEUI_CAP_DISPLAY, "the display"},
  };
  std::string out;
  for (const auto& [cap, word] : kWords) {
    bool named = false;
    for (std::size_t at = 0; at < caps.size();) {
      const std::size_t end = std::min(caps.find(' ', at), caps.size());
      named = named || caps.substr(at, end - at) == cap;
      at = end + 1;
    }
    if (named) {
      out += out.empty() ? std::string(word) : ", " + std::string(word);
    }
  }
  if (out.empty()) {
    return "Nothing named";
  }
  out[0] = static_cast<char>(out[0] - 'a' + 'A');
  return out;
}

// The Mods page's topics (D181): the mods connected now, with their notices' switch, and the quiet mods' kept notices.
// Built on GraceUI's thread at every wake and published only when they change.
protocol::Json mods_body() {
  protocol::Json rows = protocol::Json::array();
  grace::abi::ConsumerInfo info;
  for (std::uint32_t i = 0; session::consumers().info(i, info); ++i) {
    rows.push_back({{"id", info.id},
                    {"mod", info.name},
                    {"version", std::to_string(GRACEUI_VERSION_MAJOR(info.version)) + "." +
                                    std::to_string(GRACEUI_VERSION_MINOR(info.version)) + "." +
                                    std::to_string(GRACEUI_VERSION_PATCH(info.version))},
                    {"uses", uses_words(info.caps)},
                    {"notices", session::quiet().quiet(info.id) ? "Quiet" : "Shown"}});
  }
  return {{"columns",
           {{{"id", "mod"}, {"label", "Mod"}},
            {{"id", "version"}, {"label", "Version"}, {"numeric", true}},
            {{"id", "uses"}, {"label", "Uses"}},
            {{"id", "notices"},
             {"label", "Notices"},
             {"switch", {{"command", "mods.notices"}, {"key", "id"}, {"on", "Shown"}, {"off", "Quiet"}}}}}},
          {"rows", std::move(rows)}};
}

protocol::Json notices_body() {
  protocol::Json rows = protocol::Json::array();
  for (const grace::abi::QuietNotices::Kept& kept : session::quiet().kept()) {
    rows.push_back({{"mod", kept.name}, {"text", kept.text}});
  }
  return {{"columns", {{{"id", "mod"}, {"label", "Mod"}}, {{"id", "text"}, {"label", "Notice"}}}},
          {"rows", std::move(rows)}};
}

#if defined(GRACEUI_TEST_EXPORTS)
// What GraceUI_Test_GetTopic reads: a copy of each topic's text, under a lock, since a test reads while GraceUI's thread
// may be replacing the body. Made at its first use, never destroyed.
constinit grace::abi::SpinLock g_test_topics_lock;
std::map<std::string, std::string, std::less<>>& test_topics() {
  static auto* const made = new std::map<std::string, std::string, std::less<>>();
  return *made;
}
#endif

void refresh_topics(grace::spaces::OwnSpace& own) {
  for (const auto& [topic, body] : {std::pair<const char*, protocol::Json>{"graceui.mods", mods_body()},
                                    std::pair<const char*, protocol::Json>{"graceui.notices", notices_body()}}) {
    const protocol::Json* now = own.state_of(topic);
    if (now == nullptr || *now != body) {
#if defined(GRACEUI_TEST_EXPORTS)
      std::string text = body.dump();
      g_test_topics_lock.lock();
      test_topics()[topic] = std::move(text);
      g_test_topics_lock.unlock();
#endif
      own.set_state(topic, body);
    }
  }
}

// mods.notices, from either view (D181): {"id": a connected mod's id, "on": whether its notices show}.
grace::pages::Outcome mods_notices(const protocol::Cmd& command) {
  grace::pages::Outcome out;
  if (command.name != "mods.notices") {
    out.error = protocol::ErrorCode::unknown_command;
    out.detail = "Not available in this version.";
    return out;
  }
  const protocol::Json& args = command.args;
  if (!args.is_object() || !args.contains("id") || !args["id"].is_string() || !args.contains("on") ||
      !args["on"].is_boolean()) {
    out.error = protocol::ErrorCode::bad_message;
    return out;
  }
  const std::string& id = args["id"].get_ref<const std::string&>();
  std::uint32_t owner = 0;
  grace::abi::ConsumerInfo info;
  for (std::uint32_t i = 0; owner == 0 && session::consumers().info(i, info); ++i) {
    owner = id == info.id ? info.owner : 0;
  }
  if (owner == 0) {
    out.error = protocol::ErrorCode::invalid_value;
    out.detail = "That mod is not connected now.";
    return out;
  }
  const bool quiet = !args["on"].get<bool>();
  if (!session::quiet().set(id, quiet)) {
    out.error = protocol::ErrorCode::invalid_value;
    out.detail = "Too many mods are quiet already.";
    return out;
  }
  if (quiet) {
    session::notices().withdraw_owner(owner, input::now_ms());  // what it shows now goes too
  }
  log_line("%s's notices are %s", info.id, quiet ? "quiet: kept on the Mods page" : "shown again");
  if (grace::spaces::OwnSpace* own = g_own.load(std::memory_order_acquire); own != nullptr) {
    refresh_topics(*own);
  }
  keep_own_settings();
  out.ok = true;
  return out;
}

}  // namespace

void publish_ui_scale(const grace::spaces::OwnSpace& own) noexcept {
  const protocol::Json& value = own.store().held("ui.scale");
  const protocol::Setting* setting = own.store().find("ui.scale");
  const double default_value = setting != nullptr && setting->default_.is_number() ? setting->default_.get<double>() : 1.0;
  display::set_ui_scale(static_cast<float>(value.is_number() ? value.get<double>() : default_value),
                        static_cast<float>(default_value));
}

void keep_own_settings() noexcept {
  grace::spaces::OwnSpace* own = g_own.load(std::memory_order_acquire);
  if (own == nullptr || (own->revision() == g_kept_revision && session::quiet().ids_revision() == g_kept_quiet)) {
    return;
  }
  g_kept_revision = own->revision();
  g_kept_quiet = session::quiet().ids_revision();
  try {
    std::filesystem::path path;
    if (!settings_path(path)) {
      throw std::runtime_error("no folder");
    }
    std::error_code error;
    std::filesystem::create_directories(path.parent_path(), error);
    std::filesystem::path partial = path;
    partial += L".partial";
    {
      std::ofstream out(partial, std::ios::binary | std::ios::trunc);
      protocol::Json file = {{"graceui_settings", 1}, {"values", own->saved_settings()}};
      if (const std::vector<std::string> quiet = session::quiet().ids(); !quiet.empty()) {
        file["quiet_mods"] = quiet;  // the mods whose notices the player quieted (D181)
      }
      out << file.dump(2) << '\n';
      if (!out.flush()) {
        throw std::runtime_error("not written");
      }
    }
    // Whole or not at all: the new file takes the old one's place in one step.
    if (MoveFileExW(partial.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) == FALSE) {
      throw std::runtime_error("not replaced");
    }
    g_keep_failed = false;
  } catch (...) {
    if (!g_keep_failed) {
      log_line("GraceUI's settings could not be kept for the next session (its user folder is not writable)");
    }
    g_keep_failed = true;
  }
}

grace::spaces::OwnSpace* own() noexcept {
  if (g_own.load(std::memory_order_acquire) == nullptr && !g_own_failed) {
    try {
      auto* own = new grace::spaces::OwnSpace();
      std::string why;
      if (!own->load(why)) {
        g_own_failed = true;
        log_line("GraceUI's own space does not load: %s", why.c_str());
        delete own;
        return nullptr;
      }
      restore_saved(*own);  // before any view or bake reads the store
      publish_ui_scale(*own);
      g_own.store(own, std::memory_order_release);
    } catch (...) {
      g_own_failed = true;
      log_line("GraceUI's own space does not load (an exception)");
    }
  }
  return g_own.load(std::memory_order_acquire);
}

grace::spaces::OwnSpace* loaded_own() noexcept { return g_own.load(std::memory_order_acquire); }

void serve() noexcept {
  try {
    grace::spaces::OwnSpace* own_space = own();
    if (own_space == nullptr) {
      return;
    }
    if (g_server == nullptr) {
      g_server = new grace::serve::Server();
      registry().attach(*g_server);
      own_space->attach(*g_server, version_text(),
                        grace::spaces::OwnSpace::Hooks{[] { disconnect_browsers(); }, &mods_notices},
                        [] { hooks::wake(); });
    }
    // GraceUI's own module re-check, every 60 s while the server runs (§7.2 item 1): an anti-cheat loaded since the
    // first request closes it for the session.
    if (g_server->running() && steady_ms() >= g_next_recheck_ms) {
      g_next_recheck_ms = steady_ms() + recheck_interval_ms();
      if (anticheat::module_loaded()) {
        close_for_anti_cheat("GraceUI's module re-check found an anti-cheat");
      }
    }
    const bool want = wanted(*own_space);
    if (want && !g_server->running() && !g_start_failed.load(std::memory_order_relaxed)) {
      start(*own_space);
    } else if (!want && g_server->running()) {
      g_server->stop();
      g_running.store(false, std::memory_order_release);
      g_port.store(0, std::memory_order_relaxed);
      log_line("the browser view is stopped: no socket is open");
    }
    refresh_topics(*own_space);  // the Mods page: a mod came or went, or a quiet one posted (D181)
    own_space->serve();
    registry().expire(std::chrono::steady_clock::now());
    // A change from a browser while the menu is closed: the mods hear of it, and the next session keeps it (D179).
    publish_ui_scale(*own_space);
    keep_own_settings();
  } catch (...) {
    log_line("the spaces' service failed (an exception): the browser view may lag until the next wake");
  }
}

bool wants_timer() noexcept { return g_running.load(std::memory_order_acquire); }

void close_for_anti_cheat(const char* source) noexcept {
  if (!g_closed.exchange(true, std::memory_order_acq_rel)) {
    log_line("%s: the browser view is closed for the rest of the session", source);
    hooks::wake();  // GraceUI's thread stops the server at once
  }
}

void disconnect_browsers() noexcept {
  if (g_server != nullptr && g_running.load(std::memory_order_acquire)) {
    g_server->renew_session();
  }
}

#if defined(GRACEUI_TEST_EXPORTS)
void test_view(TestView& out) noexcept {
  out.running = g_running.load(std::memory_order_acquire) ? 1 : 0;
  out.port = g_port.load(std::memory_order_relaxed);
  out.spaces = static_cast<std::uint32_t>(registry().count());
  out.pending = static_cast<std::uint32_t>(registry().pending());
  out.failed = g_start_failed.load(std::memory_order_relaxed) ? 1 : 0;
}

void test_set_recheck_ms(std::uint32_t ms) noexcept { g_test_recheck_ms.store(ms, std::memory_order_relaxed); }

std::uint32_t test_topic(const char* topic, char* out, std::uint32_t capacity) noexcept {
  // Read on the caller's thread while GraceUI's thread may be writing: a test reads it only once GraceUI is idle.
  if (topic == nullptr || out == nullptr || capacity == 0) {
    return 0;
  }
  try {
    std::string text;
    g_test_topics_lock.lock();
    const auto found = test_topics().find(std::string_view(topic));
    const bool known = found != test_topics().end();
    if (known) {
      text = found->second;
    }
    g_test_topics_lock.unlock();
    if (!known || text.size() + 1 > capacity) {
      return 0;
    }
    std::memcpy(out, text.c_str(), text.size() + 1);
    return static_cast<std::uint32_t>(text.size());
  } catch (...) {
    return 0;
  }
}
#endif

}  // namespace grace::host::spaces
