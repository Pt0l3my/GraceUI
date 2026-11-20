// SPDX-License-Identifier: MIT
// host.h: grace-dev's fake host (§9.2, D168): the real grace-serve on loopback, with spaces from page data and fixtures
// in place of a game and its mods. For each space it keeps a settings store (grace-pages' SettingsStore, its one
// validating entry point, invariant 3) and publishes the settings topic from it; it publishes a state topic's fixture
// state and a stream's fixture rows when a view watches them, plays the space's scenario on a timeline, and answers every
// command once: settings.set and settings.reset by the store, dashboard.disconnect by renewing the server's session, a
// space's own commands by the scenario's reactions or ok. One thread of its own does all of it, woken by the server or by
// the scenario's next step.
//
// What it prints goes through a Printer: fixed words, a space's id, a topic, a command and a setting's key at most;
// never a setting's value, a command's arguments, a cookie, a ticket or a path (invariant 5, PV3).
#pragma once

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include "grace/serve/server.h"
#include "scenario.h"
#include "space_data.h"

namespace grace::dev {

// The increment this build is (§10, the project's version's first two numbers), which welcome names for every space
// grace-dev serves: views show a page from its increment on (§4.2.2, D173). --increment changes it.
inline constexpr std::string_view kIncrement = GRACE_DEV_INCREMENT;
// The scenario's speeds grace-dev takes: from a hundredth to a thousand times as fast.
inline constexpr double kMinSpeed = 0.01;
inline constexpr double kMaxSpeed = 1000;

enum class Out : std::uint8_t { info, warning, error };
using Printer = std::function<void(Out, std::string_view)>;

// One space to serve: a built-in one by its id, or an author's folder.
struct SpaceSource {
  std::string builtin;
  std::filesystem::path folder;
};

struct HostOptions {
  std::vector<SpaceSource> spaces;               // in welcome's order; none given: GraceUI's own and the demo space
  std::vector<std::filesystem::path> scenarios;  // --scenario: each plays in the space it names, instead of its own
  bool no_scenario = false;                      // the spaces' own scenarios stay still (--scenario files still play)
  double speed = 1;
  bool loop = false;
  std::uint16_t port = 0;                        // 0: dashboard.port's default
  std::filesystem::path web;                     // a GraceUI folder with graceui.toml, a plain folder, or none
  std::string increment = std::string(kIncrement);
  serve::Timings timings{};                      // tests shorten them
};

// What start loads and checks before anything runs.
struct Loaded {
  std::vector<SpaceData> spaces;
  std::vector<std::optional<Scenario>> scenarios;  // by space
  bool web_from_folder = false;                    // the folder's graceui.toml lists the files
  std::vector<serve::MemoryFile> web_files;        // or these, from memory
  std::vector<std::string> web_skipped;            // a plain folder's files that grace-serve would not serve
  std::uint16_t port = 0;
};

// Every space, scenario and web file of `options`, checked: false with `why` naming what failed.
bool load(const HostOptions& options, Loaded& out, std::string& why);

// grace-dev's placeholder page, served when no web folder is given.
std::string_view placeholder_page() noexcept;

// One line on a scenario as it will play: "demo: playing fixtures/demo/scenario.json, 9 steps over 60 s, once, ...".
std::string describe(const Scenario& scenario, double speed);

class DevHost {
 public:
  explicit DevHost(Printer printer = {});
  ~DevHost();
  DevHost(const DevHost&) = delete;
  DevHost& operator=(const DevHost&) = delete;

  // Loads and checks everything, then starts the server and grace-dev's thread: false with `why` (and nothing left
  // running) if a space, a scenario or the web files fail their checks, or the server does not start.
  bool start(const HostOptions& options, std::string& why);
  // grace-dev's thread joined, then the server stopped (every view told bye shutdown); idempotent.
  void stop() noexcept;

  serve::StartResult start_result() const noexcept { return started_; }
  std::uint16_t port() const noexcept { return port_; }
  bool running() const noexcept { return server_.running(); }
  serve::ServerStats stats() const noexcept { return server_.stats(); }
  // The web files read and checked by the server's worker, within `limit`.
  bool wait_files(std::chrono::milliseconds limit) const;
  const Loaded& loaded() const noexcept { return loaded_; }
  double speed() const noexcept { return speed_; }

 private:
  struct Live;

  void run() noexcept;
  void say(Out level, std::string_view line) const;

  Printer printer_;
  serve::Server server_;
  Loaded loaded_;
  std::vector<std::unique_ptr<Live>> live_;
  double speed_ = 1;
  serve::StartResult started_ = serve::StartResult::no_network;
  std::uint16_t port_ = 0;
  std::mutex mutex_;
  std::condition_variable wake_;
  bool woken_ = false;
  bool stop_ = false;
  std::thread thread_;
};

}  // namespace grace::dev
