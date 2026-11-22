// SPDX-License-Identifier: MIT
// host.cpp: grace-dev's fake host (host.h).
#include "host.h"

#include <algorithm>
#include <cstdio>
#include <deque>
#include <map>
#include <set>
#include <system_error>

#include "grace/pages/settings_store.h"
#include "grace/serve/files.h"
#include "json_text.h"

#ifndef GRACE_DEV_VERSION
#error "GRACE_DEV_VERSION is GraceUI's version, from CMakeLists.txt"
#endif

namespace grace::dev {
namespace {

using Clock = std::chrono::steady_clock;
using protocol::Json;

bool load_web(const std::filesystem::path& folder, Loaded& out, std::string& why) {
  const std::string origin = "the web folder '" + name_of(folder) + "'";
  std::error_code error;
  if (!std::filesystem::is_directory(folder, error) || error) {
    why = origin + ": not a folder";
    return false;
  }
  if (std::filesystem::is_regular_file(folder / "graceui.toml", error)) {
    out.web_from_folder = true;  // grace-serve reads and checks graceui.toml's list itself
    return true;
  }
  std::uint64_t total = 0;
  std::filesystem::recursive_directory_iterator it(folder, std::filesystem::directory_options::none, error);
  for (; !error && it != std::filesystem::recursive_directory_iterator(); it.increment(error)) {
    const std::filesystem::directory_entry& entry = *it;
    const std::u8string relative = entry.path().lexically_relative(folder).generic_u8string();
    const std::string name(relative.begin(), relative.end());
    if (entry.is_symlink(error)) {
      out.web_skipped.push_back(name);  // a link may lead out of the folder: never followed
      if (entry.is_directory(error)) {
        it.disable_recursion_pending();
      }
      continue;
    }
    if (entry.is_directory(error)) {
      if (name_of(entry.path()).starts_with(".")) {
        it.disable_recursion_pending();  // a hidden folder's files would not be served anyway
      }
      continue;
    }
    if (!entry.is_regular_file(error)) {
      continue;
    }
    if (!serve::valid_web_name(name) || serve::content_type_of(name).empty()) {
      out.web_skipped.push_back(name);
      continue;
    }
    if (out.web_files.size() == serve::kMaxListedFiles) {
      why = origin + ": more than " + std::to_string(serve::kMaxListedFiles) + " files to serve";
      return false;
    }
    auto bytes = std::make_shared<std::string>();
    std::string bad;
    if (!read_file(entry.path(), serve::kMaxWebFile, *bytes, bad)) {
      why = origin + ": " + name + ": " + bad;
      return false;
    }
    total += bytes->size();
    if (total > serve::kMaxWebTotal) {
      why = origin + ": more than " + std::to_string(serve::kMaxWebTotal >> 20) + " MiB of files to serve";
      return false;
    }
    out.web_files.push_back(serve::MemoryFile{name, std::move(bytes)});
  }
  if (error) {
    why = origin + ": unreadable";
    return false;
  }
  std::sort(out.web_files.begin(), out.web_files.end(),
            [](const serve::MemoryFile& a, const serve::MemoryFile& b) { return a.name < b.name; });
  return true;
}

}  // namespace

std::string describe(const Scenario& scenario, double speed) {
  const double seconds = static_cast<double>(scenario.length) / speed / 1000.0;
  char time[32];
  std::snprintf(time, sizeof time, seconds < 10 ? "%.2f s" : "%.0f s", seconds);
  return scenario.space + ": playing " + scenario.name + ", " + std::to_string(scenario.steps.size()) +
         " steps over " + time + (scenario.loop ? ", again and again" : ", once") + ", with " +
         std::to_string(scenario.reactions.size()) + " reactions";
}

std::string_view placeholder_page() noexcept {
  return "<!DOCTYPE html>\n"
         "<html lang=\"en-GB\"><head><meta charset=\"utf-8\"><title>grace-dev</title></head><body>\n"
         "<h1>grace-dev is running</h1>\n"
         "<p>No web app was given, so this page stands in for it. The spaces are served on this address all the "
         "same.</p>\n"
         "<p>To serve the web app, start grace-dev again with --web and its folder: a built web app (web/dist), or a "
         "GraceUI folder whose graceui.toml lists its files.</p>\n"
         "</body></html>\n";
}

bool load(const HostOptions& options, Loaded& out, std::string& why) {
  out = Loaded{};
  std::vector<SpaceSource> sources = options.spaces;
  if (sources.empty()) {
    for (const std::string& id : builtin_ids()) {
      sources.push_back(SpaceSource{id, {}});
    }
  }
  for (const SpaceSource& source : sources) {
    SpaceData space;
    const bool ok = source.builtin.empty() ? load_folder(source.folder, space, why)
                                           : load_builtin(source.builtin, space, why);
    if (!ok) {
      return false;
    }
    for (const SpaceData& other : out.spaces) {
      if (other.id == space.id) {
        why = space.origin + ": its id, " + space.id + ", is " + other.origin + "'s too";
        return false;
      }
    }
    out.spaces.push_back(std::move(space));
  }
  out.scenarios.resize(out.spaces.size());
  for (std::size_t i = 0; i < out.spaces.size(); ++i) {
    const SpaceData& space = out.spaces[i];
    if (options.no_scenario || space.scenario_text.empty()) {
      continue;
    }
    Scenario scenario;
    if (!load_scenario(space.scenario_text, space.scenario_name, space, scenario, why)) {
      why = space.origin + ": " + why;
      return false;
    }
    out.scenarios[i] = std::move(scenario);
  }
  std::set<std::string, std::less<>> replaced;
  for (const std::filesystem::path& file : options.scenarios) {
    const std::string name = name_of(file);
    std::string text;
    std::string bad;
    std::string id;
    if (!read_file(file, kMaxFixtureFile, text, bad)) {
      why = name + ": " + bad;
      return false;
    }
    if (!scenario_space(text, name, id, why)) {
      return false;
    }
    const auto found = std::find_if(out.spaces.begin(), out.spaces.end(),
                                    [&](const SpaceData& space) { return space.id == id; });
    if (found == out.spaces.end()) {
      why = name + ": it plays in the space " + id + ", which grace-dev is not serving";
      return false;
    }
    if (!replaced.insert(id).second) {
      why = name + ": another --scenario plays in the space " + id + " already";
      return false;
    }
    Scenario scenario;
    if (!load_scenario(text, name, *found, scenario, why)) {
      return false;
    }
    out.scenarios[static_cast<std::size_t>(found - out.spaces.begin())] = std::move(scenario);
  }
  for (std::optional<Scenario>& scenario : out.scenarios) {
    if (scenario.has_value()) {
      scenario->loop = scenario->loop || options.loop;
      if (scenario->loop && scenario->length == 0) {
        why = scenario->name + ": it loops, but has no length: give it length_ms, or a step after 0 ms";
        return false;
      }
    }
  }
  if (!(options.speed >= kMinSpeed && options.speed <= kMaxSpeed)) {
    why = "the speed is from 0.01 to 1000";
    return false;
  }
  if (!options.web.empty() && !load_web(options.web, out, why)) {
    return false;
  }
  if (options.web.empty()) {
    out.web_files.push_back(
        serve::MemoryFile{"index.html", std::make_shared<const std::string>(std::string(placeholder_page()))});
  }
  out.port = options.port != 0 ? options.port : default_port();
  return true;
}

// ---- One space, served.

struct DevHost::Live {
  struct StateTopic {
    Json body;
    bool has_body = false;
    Json fixture;
    bool has_fixture = false;
    std::uint64_t rev = 1;
  };
  struct StreamTopic {
    std::deque<Json> rows;
    std::uint64_t next_seq = 1;
    std::size_t ring = kDefaultRing;
  };

  DevHost& host;
  const SpaceData& data;
  const Scenario* scenario = nullptr;
  pages::SettingsStore store;
  std::shared_ptr<serve::SpacePort> port;
  std::uint64_t settings_rev = 1;
  std::map<std::string, StateTopic, std::less<>> states;
  std::map<std::string, StreamTopic, std::less<>> streams;
  std::set<std::string, std::less<>> watched;
  std::set<std::string, std::less<>> said;  // topics watched with nothing to publish, and failures, each said once
  // The scenario's place.
  std::size_t next_step = 0;
  Clock::time_point pass_start{};
  bool finished = false;

  Live(DevHost& owner, const SpaceData& space, const Scenario* script) : host(owner), data(space), scenario(script) {
    std::string why;
    store.load(data.settings, why);  // checked when the space was loaded
    for (auto it = data.fixtures.begin(); it != data.fixtures.end(); ++it) {
      const protocol::TopicInfo* topic = data.topic(it.key());
      if (topic != nullptr && topic->family.value_or(false) && topic->id != it.key()) {
        StateTopic& member = states[it.key()];  // a family's member (D215), published when a view watches it
        member.body = it.value();
        member.fixture = it.value();
        member.has_body = member.has_fixture = true;
      }
    }
    for (const protocol::TopicInfo& topic : data.pages.topics) {
      if (topic.id == "settings" || topic.family.value_or(false)) {
        continue;
      }
      const Json* fixture = data.fixtures.contains(topic.id) ? &data.fixtures[topic.id] : nullptr;
      if (topic.shape == protocol::TopicShape::state) {
        StateTopic& state = states[topic.id];
        if (fixture != nullptr) {
          state.body = *fixture;
          state.fixture = *fixture;
          state.has_body = state.has_fixture = true;
        }
      } else {
        StreamTopic& stream = streams[topic.id];
        stream.ring = ring_of(topic);
        if (fixture != nullptr) {
          for (const Json& row : *fixture) {
            stream.rows.push_back(row);
          }
          stream.next_seq = stream.rows.back()["seq"].get<std::uint64_t>() + 1;
        }
      }
    }
  }

  Clock::duration real(std::uint64_t ms) const {
    return std::chrono::duration_cast<Clock::duration>(
        std::chrono::duration<double, std::milli>(static_cast<double>(ms) / host.speed_));
  }

  void say_once(const std::string& what, Out level, const std::string& line) {
    if (said.insert(what).second) {
      host.say(level, data.id + ": " + line);
    }
  }

  void publish(protocol::Message message, const std::string& topic) {
    if (!port->publish(std::move(message))) {
      say_once("failed " + topic, Out::warning,
               topic + " could not be published: past a frame's limits, or the server's queue is full");
    }
  }

  void publish_settings() {
    protocol::SettingsBody body;
    body.schema = store.description().schema;
    body.description = store.description().settings;
    body.values = store.values();  // a secret's as "set" or "" (invariant 5)
    body.changed = store.changed();
    if (scenario != nullptr) {
      std::vector<protocol::InheritedValue> inherited;
      for (const protocol::InheritedValue& value : scenario->inherited) {
        if (store.held(value.key) == "inherit") {
          inherited.push_back(value);
        }
      }
      if (!inherited.empty()) {
        body.inherited = std::move(inherited);
      }
    }
    protocol::State state;
    state.topic = "settings";
    state.rev = settings_rev;
    state.body = protocol::encode(body);
    publish(protocol::Message{std::move(state)}, "settings");
  }

  void publish_state(const std::string& id) {
    const StateTopic& topic = states[id];
    if (!topic.has_body) {
      say_once("empty " + id, Out::info, id + " is watched, but has no fixture state: it publishes nothing");
      return;
    }
    protocol::State state;
    state.topic = id;
    state.rev = topic.rev;
    state.body = topic.body;
    publish(protocol::Message{std::move(state)}, id);
  }

  // Rows from `first` (an index into the topic's rows) to its end, in batches.
  void publish_rows(const std::string& id, std::size_t first) {
    const StreamTopic& topic = streams[id];
    for (std::size_t from = first; from < topic.rows.size(); from += protocol::kRowsPerBatch) {
      const std::size_t to = std::min(topic.rows.size(), from + protocol::kRowsPerBatch);
      protocol::Rows rows;
      rows.topic = id;
      rows.from_seq = topic.rows[from]["seq"].get<std::uint64_t>();
      rows.rows.assign(topic.rows.begin() + static_cast<std::ptrdiff_t>(from),
                       topic.rows.begin() + static_cast<std::ptrdiff_t>(to));
      publish(protocol::Message{std::move(rows)}, id);
    }
  }

  void publish_topic(const std::string& id) {
    if (id == "settings") {
      publish_settings();
    } else if (states.find(id) != states.end()) {
      publish_state(id);
    } else if (streams.find(id) != streams.end()) {
      publish_rows(id, 0);  // from the oldest row still held (space.h)
    }
  }

  bool is_watched(std::string_view id) const { return watched.find(id) != watched.end(); }

  void apply(const Action& action) {
    if (action.kind == Action::Kind::state) {
      StateTopic& topic = states[action.topic];
      topic.body = action.body;
      topic.has_body = true;
      ++topic.rev;
      if (is_watched(action.topic)) {
        publish_state(action.topic);
      }
      return;
    }
    StreamTopic& topic = streams[action.topic];
    if (action.kind == Action::Kind::clear) {
      topic.rows.clear();
      ++topic.next_seq;  // a gap: the stream starts over there, for the server and every view (D161)
      if (is_watched(action.topic)) {
        protocol::Rows cleared;  // an empty batch past the gap clears it in every view now (protocol 1.1)
        cleared.topic = action.topic;
        cleared.from_seq = topic.next_seq;
        publish(protocol::Message{std::move(cleared)}, action.topic);
      }
      return;
    }
    const std::size_t before = topic.rows.size();
    for (const Json& row : action.rows) {
      Json numbered = Json::object();
      numbered["seq"] = topic.next_seq++;
      for (auto it = row.begin(); it != row.end(); ++it) {
        numbered[it.key()] = it.value();
      }
      topic.rows.push_back(std::move(numbered));
    }
    std::size_t first = before;
    while (topic.rows.size() > topic.ring) {
      topic.rows.pop_front();
      first = first > 0 ? first - 1 : 0;
    }
    if (is_watched(action.topic)) {
      publish_rows(action.topic, first);
    }
  }

  // A new pass starts from the fixture's states: each state topic the scenario changes goes back to its fixture.
  void restart_pass() {
    std::set<std::string, std::less<>> touched;
    for (const Step& step : scenario->steps) {
      if (step.action.kind == Action::Kind::state) {
        touched.insert(step.action.topic);
      }
    }
    for (const std::string& id : touched) {
      StateTopic& topic = states[id];
      if (topic.has_fixture && topic.body != topic.fixture) {
        topic.body = topic.fixture;
        ++topic.rev;
        if (is_watched(id)) {
          publish_state(id);
        }
      }
    }
  }

  Clock::time_point next_due() const {
    if (scenario == nullptr || finished) {
      return Clock::time_point::max();
    }
    if (next_step < scenario->steps.size()) {
      return pass_start + real(scenario->steps[next_step].at);
    }
    return scenario->loop ? pass_start + real(scenario->length) : Clock::time_point::max();
  }

  void play(Clock::time_point now) {
    while (scenario != nullptr && !finished) {
      if (next_step < scenario->steps.size()) {
        if (pass_start + real(scenario->steps[next_step].at) > now) {
          return;
        }
        apply(scenario->steps[next_step].action);
        ++next_step;
        continue;
      }
      if (!scenario->loop) {
        finished = true;
        if (!scenario->steps.empty()) {
          host.say(Out::info, data.id + ": the scenario has played to its end");
        }
        return;
      }
      const Clock::time_point due = pass_start + real(scenario->length);
      if (due > now) {
        return;
      }
      // Passes missed (a fast speed, a slow machine) are skipped rather than played in a burst.
      pass_start = now - due > real(scenario->length) ? now : due;
      next_step = 0;
      restart_pass();
    }
  }

  static std::string key_of(const Json& args, const pages::SettingsStore& store, const SpaceData& data) {
    for (const char* field : {"key", "target"}) {
      if (args.is_object() && args.contains(field) && args[field].is_string()) {
        const std::string& named = args[field].get_ref<const std::string&>();
        // Named only if it is a known key or page: never a string a view made up (it could hold anything).
        if (store.find(named) != nullptr || data.has_page(named)) {
          return " " + named;
        }
      }
    }
    return "";
  }

  void command(const serve::SpaceEvent& event) {
    protocol::Result result;
    std::string label = event.command;
    std::string done;
    const auto refuse = [&](protocol::ErrorCode code, std::string detail) {
      result.error = code;
      result.detail = std::move(detail);  // the reason in words, never a value (§6.7; protocol 1.1)
    };
    bool renew = false;
    if (event.command == "settings.set" || event.command == "settings.reset") {
      protocol::Cmd cmd;
      cmd.space = data.id;
      cmd.id = event.reply.id;
      cmd.name = event.command;
      cmd.args = event.args;
      const pages::Outcome outcome = store.apply(cmd);
      label += key_of(event.args, store, data);
      if (outcome.ok) {
        result.ok = true;
        if (!outcome.changed.empty()) {
          ++settings_rev;
          done = ", " + std::to_string(outcome.changed.size()) + " changed";
          if (is_watched("settings")) {
            publish_settings();
          }
        }
      } else {
        refuse(outcome.error, outcome.detail);
      }
    } else if (event.command == "dashboard.disconnect") {
      result.ok = true;  // answered first, then every connection closes (§6.6)
      renew = true;
      done = ", every browser disconnected";
    } else if (event.command == "menu.close") {
      refuse(protocol::ErrorCode::not_now, "There is no menu in game here: grace-dev serves the browser view alone.");
    } else if (const Reaction* reaction = scenario != nullptr ? scenario->reaction(event.command) : nullptr) {
      for (const Action& action : reaction->actions) {
        apply(action);
      }
      if (reaction->ok) {
        result.ok = true;
      } else {
        refuse(reaction->error, reaction->detail);
      }
      done = ", the scenario's reaction";
    } else {
      result.ok = true;  // a space's own command with no reaction: ok, and nothing changes
    }
    const std::string answer = result.error.has_value() ? std::string(protocol::to_string(*result.error)) : "ok";
    if (!port->answer(event.reply, std::move(result))) {
      host.say(Out::warning, data.id + ": " + label + ": its answer could not be sent");
    }
    if (renew) {
      host.server_.renew_session();
    }
    host.say(Out::info, data.id + ": " + label + ": " + answer + done);
  }

  void drain() {
    serve::SpaceEvent event;
    while (port->poll(event)) {
      switch (event.kind) {
        case serve::SpaceEvent::Kind::watched:
          watched.insert(event.topic);
          publish_topic(event.topic);
          break;
        case serve::SpaceEvent::Kind::unwatched:
          watched.erase(event.topic);
          break;
        case serve::SpaceEvent::Kind::command:
          command(event);
          break;
        case serve::SpaceEvent::Kind::none:
          break;
      }
    }
  }
};

DevHost::DevHost(Printer printer) : printer_(std::move(printer)) {}

DevHost::~DevHost() { stop(); }

void DevHost::say(Out level, std::string_view line) const {
  if (printer_) {
    printer_(level, line);
  }
}

bool DevHost::start(const HostOptions& options, std::string& why) {
  if (thread_.joinable() || server_.running()) {
    why = "grace-dev is running already";
    return false;
  }
  for (const auto& live : live_) {
    server_.remove_space(live->data.id);  // an earlier start's
  }
  live_.clear();
  if (!load(options, loaded_, why)) {
    return false;
  }
  speed_ = options.speed;
  port_ = loaded_.port;
  for (std::size_t i = 0; i < loaded_.spaces.size(); ++i) {
    const SpaceData& space = loaded_.spaces[i];
    const Scenario* scenario = loaded_.scenarios[i].has_value() ? &*loaded_.scenarios[i] : nullptr;
    auto live = std::make_unique<Live>(*this, space, scenario);
    live->port = server_.add_space(serve::describe_space(space.pages, space.settings, GRACE_DEV_VERSION,
                                                          options.increment),
                                   [this] {
                                     std::lock_guard lock(mutex_);
                                     woken_ = true;
                                     wake_.notify_all();
                                   });
    if (live->port == nullptr) {
      why = space.origin + ": the server took no space of its id";
      live_.clear();
      return false;
    }
    live_.push_back(std::move(live));
  }
  serve::ServerConfig config;
  config.port = port_;
  config.graceui_version = GRACE_DEV_VERSION;
  config.from_memory = !loaded_.web_from_folder;
  config.folder = loaded_.web_from_folder ? options.web : std::filesystem::path();
  config.memory_files = loaded_.web_files;
  config.timings = options.timings;
  config.log = [this](serve::LogLevel level, std::string_view line) {
    say(level == serve::LogLevel::error     ? Out::error
        : level == serve::LogLevel::warning ? Out::warning
                                            : Out::info,
        "grace-serve: " + std::string(line));
  };
  started_ = server_.start(config);
  if (started_ != serve::StartResult::ok) {
    why = "the server did not start on port " + std::to_string(port_) + ": " + std::string(serve::to_string(started_));
    if (started_ == serve::StartResult::port_taken || started_ == serve::StartResult::port_reserved) {
      why += " (choose another with --port)";
    }
    for (const auto& live : live_) {
      server_.remove_space(live->data.id);
    }
    live_.clear();
    return false;
  }
  const Clock::time_point now = Clock::now();
  for (const auto& live : live_) {
    live->pass_start = now;
  }
  {
    std::lock_guard lock(mutex_);
    stop_ = false;
    woken_ = true;
  }
  thread_ = std::thread([this] { run(); });
  return true;
}

void DevHost::stop() noexcept {
  {
    std::lock_guard lock(mutex_);
    stop_ = true;
    wake_.notify_all();
  }
  if (thread_.joinable()) {
    thread_.join();
  }
  server_.stop();
}

bool DevHost::wait_files(std::chrono::milliseconds limit) const {
  const Clock::time_point end = Clock::now() + limit;
  while (!server_.stats().files_ready) {
    if (Clock::now() >= end) {
      return false;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }
  return true;
}

void DevHost::run() noexcept {
  try {
    std::unique_lock lock(mutex_);
    while (!stop_) {
      Clock::time_point due = Clock::time_point::max();
      for (const auto& live : live_) {
        due = std::min(due, live->next_due());
      }
      if (!woken_) {
        if (due == Clock::time_point::max()) {
          wake_.wait(lock, [this] { return woken_ || stop_; });
        } else {
          wake_.wait_until(lock, due, [this] { return woken_ || stop_; });
        }
      }
      if (stop_) {
        break;
      }
      woken_ = false;
      lock.unlock();
      for (const auto& live : live_) {
        live->drain();
        live->play(Clock::now());
      }
      lock.lock();
    }
  } catch (...) {
    say(Out::error, "grace-dev's thread stopped after a fault: its spaces no longer answer");
  }
}

}  // namespace grace::dev
