// harness.cpp: the suite's server, its web folder and its two spaces (harness.h).
#include "harness.h"

#include <doctest/doctest.h>

#include <fstream>
#include <random>

#include "grace/pages/builtin.h"
#include "grace/serve/codec.h"

namespace grace::serve::iotest {
namespace {

std::string hex_sha256(std::string_view text) {
  Sha256 sha;
  sha.update(text);
  static constexpr char kDigits[] = "0123456789abcdef";
  std::string out;
  for (const std::uint8_t byte : sha.finish()) {
    out.push_back(kDigits[byte >> 4]);
    out.push_back(kDigits[byte & 15]);
  }
  return out;
}

void write_file(const std::filesystem::path& path, std::string_view text) {
  std::filesystem::create_directories(path.parent_path());
  std::ofstream out(path, std::ios::binary);
  out.write(text.data(), static_cast<std::streamsize>(text.size()));
}

std::string listed(std::string_view name, std::string_view text) {
  return "\"" + std::string(name) + "\" = { size = " + std::to_string(text.size()) + ", sha256 = \"" +
         hex_sha256(text) + "\" }\n";
}

}  // namespace

Timings fast_timings() {
  Timings timings;
  timings.head = 400ms;
  timings.hello = 400ms;
  timings.ticket = 1500ms;
  timings.ping = 30s;
  timings.close_wait = 300ms;
  timings.write = 2s;
  timings.state_interval = 10ms;
  timings.rate_window = 1000ms;
  return timings;
}

protocol::Json frame(std::string_view kind, protocol::Json fields) {
  protocol::Json out = {{"v", 1}, {"t", std::string(kind)}};
  for (auto& [key, value] : fields.items()) {
    out[key] = value;
  }
  return out;
}

SpaceDescription builtin_description(std::string_view id) {
  for (const pages::BuiltinSpace& space : pages::builtin_spaces()) {
    if (space.id != id) {
      continue;
    }
    protocol::SpacePages pages;
    protocol::SettingsDescription settings;
    protocol::DecodeError error;
    const bool decoded = protocol::decode(protocol::Json::parse(space.pages), pages, "", error) &&
                         protocol::decode(protocol::Json::parse(space.settings), settings, "", error);
    REQUIRE_MESSAGE(decoded, error.field, ": ", error.why);
    return describe_space(pages, settings, "0.2.0", "0.2");
  }
  FAIL("no built-in space ", std::string(id));
  return {};
}

protocol::Json demo_fixtures() {
  std::ifstream in(std::filesystem::path(GRACE_FIXTURES) / "demo" / "topics.json", std::ios::binary);
  const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  protocol::Json json = protocol::Json::parse(text, nullptr, false);
  REQUIRE(json.is_object());
  return json;
}

TestSpace::TestSpace(Server& server, SpaceDescription description, protocol::Json fixtures)
    : fixtures_(std::move(fixtures)) {
  port = server.add_space(std::move(description), [this] {
    std::lock_guard lock(mutex_);
    woken_ = true;
    wake_.notify_all();
  });
  REQUIRE(port != nullptr);
  thread_ = std::thread([this] { run(); });
}

TestSpace::~TestSpace() {
  {
    std::lock_guard lock(mutex_);
    stop_ = true;
    wake_.notify_all();
  }
  thread_.join();
}

void TestSpace::hold(bool on) {
  std::lock_guard lock(mutex_);
  held_ = on;
  woken_ = true;
  wake_.notify_all();
}

void TestSpace::set_on_command(std::function<void(const SpaceEvent&)> handler) {
  std::lock_guard lock(mutex_);
  on_command_ = std::move(handler);
}

std::vector<std::string> TestSpace::commands() {
  std::lock_guard lock(mutex_);
  return commands_;
}

std::vector<std::string> TestSpace::watched() {
  std::lock_guard lock(mutex_);
  return watched_;
}

std::vector<std::string> TestSpace::events() {
  std::lock_guard lock(mutex_);
  return events_;
}

bool TestSpace::wait_for(const std::function<bool()>& condition, Millis limit) {
  const auto end = std::chrono::steady_clock::now() + limit;
  while (std::chrono::steady_clock::now() < end) {
    if (condition()) {
      return true;
    }
    std::this_thread::sleep_for(5ms);
  }
  return condition();
}

void TestSpace::publish_topic(const std::string& topic) {
  const auto& topics = port->description().topics;
  const auto info = std::find_if(topics.begin(), topics.end(), [&](const auto& t) { return t.id == topic; });
  if (info == topics.end()) {
    return;
  }
  if (info->shape == protocol::TopicShape::state) {
    protocol::State state;
    state.topic = topic;
    state.rev = 1;
    state.body = fixtures_.contains(topic) ? fixtures_[topic] : protocol::Json{{"fixture", topic}};
    port->publish(protocol::Message{std::move(state)});
  } else if (fixtures_.contains(topic) && !fixtures_[topic].empty()) {
    protocol::Rows rows;
    rows.topic = topic;
    rows.from_seq = fixtures_[topic][0]["seq"].get<std::uint64_t>();
    for (const auto& row : fixtures_[topic]) {
      rows.rows.push_back(row);
    }
    port->publish(protocol::Message{std::move(rows)});
  }
}

void TestSpace::run() {
  while (true) {
    std::function<void(const SpaceEvent&)> handler;
    {
      std::unique_lock lock(mutex_);
      wake_.wait_for(lock, 20ms, [this] { return woken_ || stop_; });
      if (stop_) {
        return;
      }
      woken_ = false;
      if (held_) {
        continue;
      }
      handler = on_command_;
    }
    SpaceEvent event;
    while (port->poll(event)) {
      std::unique_lock lock(mutex_);
      switch (event.kind) {
        case SpaceEvent::Kind::watched:
          watched_.push_back(event.topic);
          events_.push_back("watched " + event.topic);
          break;
        case SpaceEvent::Kind::unwatched:
          std::erase(watched_, event.topic);
          events_.push_back("unwatched " + event.topic);
          break;
        case SpaceEvent::Kind::command:
          commands_.push_back(event.command);
          events_.push_back("command " + event.command);
          break;
        case SpaceEvent::Kind::none:
          break;
      }
      lock.unlock();
      if (event.kind == SpaceEvent::Kind::watched) {
        publish_topic(event.topic);
      } else if (event.kind == SpaceEvent::Kind::command) {
        if (handler) {
          handler(event);
        } else {
          protocol::Result result;
          result.ok = true;
          port->answer(event.reply, std::move(result));
        }
      }
    }
  }
}

WebFolder::WebFolder(bool damage_index, bool list_missing) {
  std::random_device random;
  path = std::filesystem::temp_directory_path() / ("grace-serve-test-" + std::to_string(random()) + "-" +
                                                   std::to_string(random()));
  index = "<!DOCTYPE html>\n<html lang=\"en-GB\"><title>Lantern</title><p>An invented page for the tests.</p>\n";
  script = "console.log('the lantern is lit');\n";
  std::string toml =
      "# GraceUI's version file, as its release job writes it (invented values for the tests)\n"
      "graceui = \"0.2.0\"\nabi = [1, 1]\nprotocol = [1, 0]\ncaps = []\nreleased = \"2026-11-20\"\n\n[files]\n";
  toml += listed("graceui.dll", "not here");
  toml += listed("web/index.html", index);
  toml += listed("web/assets/app.js", script);
  if (list_missing) {
    toml += listed("web/missing.css", "body {}");
  }
  write_file(path / "graceui.toml", toml);
  std::string written = index;
  if (damage_index) {
    written[written.size() - 2] = '?';  // the same size, another hash
  }
  write_file(path / "web" / "index.html", written);
  write_file(path / "web" / "assets" / "app.js", script);
  write_file(path / "web" / "stray.html", "<p>Not listed.</p>\n");
}

WebFolder::~WebFolder() {
  std::error_code error;
  std::filesystem::remove_all(path, error);
}

Fixture::Fixture() : Fixture(Options{}) {}

Fixture::Fixture(Options options) {
  folder = std::make_unique<WebFolder>(options.damaged);
  if (options.spaces) {
    graceui = std::make_unique<TestSpace>(server, builtin_description("graceui"), protocol::Json::object());
    demo = std::make_unique<TestSpace>(server, builtin_description("demo"), demo_fixtures());
  }
  for (int attempt = 0; attempt < 5; ++attempt) {
    ServerConfig config;
    config.port = free_port();
    config.graceui_version = "0.2.0";
    config.timings = options.timings;
    config.send_queue = options.send_queue;
    config.folder = options.no_folder ? folder->path / "nothing-here" : folder->path;
    if (options.from_memory) {
      config.from_memory = true;
      config.memory_files.push_back(MemoryFile{"index.html", std::make_shared<const std::string>(folder->index)});
      config.memory_files.push_back(
          MemoryFile{"assets/app.js", std::make_shared<const std::string>(folder->script)});
    }
    config.log = [this](LogLevel, std::string_view line) {
      std::lock_guard lock(log_mutex_);
      logs_.emplace_back(line);
    };
    started = server.start(config);
    port = config.port;
    if (started != StartResult::port_taken) {
      break;
    }
  }
  REQUIRE(started == StartResult::ok);
}

Fixture::~Fixture() {
  server.stop();
  graceui.reset();
  demo.reset();
  folder.reset();
}

std::vector<std::string> Fixture::logs() {
  std::lock_guard lock(log_mutex_);
  return logs_;
}

bool Fixture::wait_files(Millis limit) {
  const auto end = std::chrono::steady_clock::now() + limit;
  while (std::chrono::steady_clock::now() < end) {
    if (server.stats().files_ready) {
      return true;
    }
    std::this_thread::sleep_for(5ms);
  }
  return false;
}

std::string Fixture::cookie() { return cookie_from(request(port, page_load(port))); }

std::string Fixture::ticket(const std::string& cookie) { return fetch_ticket(port, cookie); }

std::unique_ptr<WsClient> Fixture::join(protocol::Json* welcome) {
  const std::string session = cookie();
  REQUIRE(session.size() == 43);
  const std::string one = ticket(session);
  REQUIRE(one.size() == 43);
  auto client = std::make_unique<WsClient>();
  REQUIRE(client->join(port, session, one, welcome));
  return client;
}

}  // namespace grace::serve::iotest
