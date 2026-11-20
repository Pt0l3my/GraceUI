// spaces.cpp: the spaces in the host, and the browser view's server (spaces.h; D169, D161).
#include "spaces.h"

#include <windows.h>

#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string>

#include "display.h"
#include "draw_atlas.h"
#include "grace/serve/server.h"
#include "grace/spaces/own_space.h"
#include "grace/spaces/registry.h"
#include "hooks.h"
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
constinit bool g_keep_failed = false;
constinit std::atomic<bool> g_running{false};
constinit std::atomic<std::uint32_t> g_port{0};

void server_log(grace::serve::LogLevel, std::string_view line) {
  log_line("browser view: %.*s", static_cast<int>(line.size()), line.data());
}

bool wanted(const grace::spaces::OwnSpace& own) {
  const protocol::Json& enabled = own.store().held("dashboard.enabled");
  return !session::notice_only() && registry().wants_browser() && enabled.is_boolean() && enabled.get<bool>();
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
  if (own == nullptr || own->revision() == g_kept_revision) {
    return;
  }
  g_kept_revision = own->revision();
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
      const protocol::Json file = {{"graceui_settings", 1}, {"values", own->saved_settings()}};
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
      own_space->attach(*g_server, version_text(), grace::spaces::OwnSpace::Hooks{[] { disconnect_browsers(); }},
                        [] { hooks::wake(); });
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
    own_space->serve();
    registry().expire(std::chrono::steady_clock::now());
  } catch (...) {
    log_line("the spaces' service failed (an exception): the browser view may lag until the next wake");
  }
}

bool wants_timer() noexcept { return g_running.load(std::memory_order_acquire); }

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
#endif

}  // namespace grace::host::spaces
