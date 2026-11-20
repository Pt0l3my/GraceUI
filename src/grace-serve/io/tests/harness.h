// harness.h: the security suite's server (§9.1): a real grace-serve on a free loopback port, with short deadlines, a web
// folder of invented files and its graceui.toml, and two spaces, GraceUI's own and the demo space from grace-pages,
// each backed by a test space on a thread of its own that answers from fixtures/demo/topics.json.
#pragma once

#include <atomic>
#include <condition_variable>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "client.h"
#include "grace/serve/server.h"

namespace grace::serve::iotest {

// The suite's deadlines: the defaults' shapes, short enough that it runs in seconds.
Timings fast_timings();

// A space a test drives: its port, and a thread that publishes its fixture topics when watched and answers its
// commands ok (or as `on_command` says), unless held.
class TestSpace {
 public:
  TestSpace(Server& server, SpaceDescription description, protocol::Json fixtures);
  ~TestSpace();
  TestSpace(const TestSpace&) = delete;
  TestSpace& operator=(const TestSpace&) = delete;

  std::shared_ptr<SpacePort> port;

  // While held, commands wait in the port's queue.
  void hold(bool on);
  // Called for each command instead of answering ok; it answers through `port` itself.
  void set_on_command(std::function<void(const SpaceEvent&)> handler);

  std::vector<std::string> commands();  // the names taken, in order
  std::vector<std::string> watched();   // topics watched now
  std::vector<std::string> events();    // "watched x", "unwatched x", "command y", in order
  bool wait_for(const std::function<bool()>& condition, Millis limit = 2s);

 private:
  void run();
  void publish_topic(const std::string& topic);

  protocol::Json fixtures_;
  std::function<void(const SpaceEvent&)> on_command_;
  std::mutex mutex_;
  std::condition_variable wake_;
  bool woken_ = false;
  bool stop_ = false;
  bool held_ = false;
  std::vector<std::string> commands_;
  std::vector<std::string> watched_;
  std::vector<std::string> events_;
  std::thread thread_;
};

// The built-in spaces' descriptions (graceui, then demo), from grace-pages.
SpaceDescription builtin_description(std::string_view id);
// fixtures/demo/topics.json.
protocol::Json demo_fixtures();

// A web folder of our own: graceui.toml and web/ with index.html, assets/app.js and a stray file it does not list.
struct WebFolder {
  explicit WebFolder(bool damage_index = false, bool list_missing = false);
  ~WebFolder();
  std::filesystem::path path;
  std::string index;
  std::string script;
};

struct Fixture {
  struct Options {
    Timings timings = fast_timings();
    std::size_t send_queue = 512 * 1024;
    bool damaged = false;        // index.html's bytes differ from its listed hash
    bool from_memory = false;
    bool no_folder = false;      // the folder holds nothing
    bool spaces = true;
  };
  Fixture();
  explicit Fixture(Options options);
  ~Fixture();

  Server server;
  std::uint16_t port = 0;
  StartResult started = StartResult::no_network;
  std::unique_ptr<WebFolder> folder;
  std::unique_ptr<TestSpace> graceui;
  std::unique_ptr<TestSpace> demo;

  std::vector<std::string> logs();
  // A page load's cookie, a ticket with it, and a WebSocket joined with both.
  std::string cookie();
  std::string ticket(const std::string& cookie);
  std::unique_ptr<WsClient> join(protocol::Json* welcome = nullptr);
  bool wait_files(Millis limit = 2s);

 private:
  std::mutex log_mutex_;
  std::vector<std::string> logs_;
};

protocol::Json frame(std::string_view kind, protocol::Json fields = protocol::Json::object());

}  // namespace grace::serve::iotest
