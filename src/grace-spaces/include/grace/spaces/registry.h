// SPDX-License-Identifier: MIT
// registry.h: the mods' spaces across the ABI (D169, §6.2, D75, D81): each mod's one space, registered with its page
// data and settings description, checked, and handed to grace-serve when it asks for the browser view; the bridge
// protocol's frames in both directions, the same JSON frames as the WebSocket: a mod publishes state and rows frames,
// polls watched, unwatched and command events as sub, unsub and cmd frames, and answers each command with a result frame
// by the reply handle its event gave, once, within kAnswerDeadline or GraceUI answers it `internal`. GraceUI routes and
// checks; the space validates its own commands (invariant 3). Every method is thread-safe and never waits for a view:
// the mods call it from threads of their own, GraceUI's own thread from its own. No Win32: tested on Linux against the
// real server.
#pragma once

#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

#include "grace/protocol/messages.h"
#include "grace/serve/server.h"
#include "grace/serve/space.h"

namespace grace::spaces {

inline constexpr std::uint32_t kViewGame = 1;     // GRACEUI_VIEW_GAME
inline constexpr std::uint32_t kViewBrowser = 2;  // GRACEUI_VIEW_BROWSER
inline constexpr std::size_t kMaxSpaces = 16;     // mods' spaces in a session (EST)
inline constexpr std::chrono::milliseconds kAnswerDeadline{10000};  // a command unanswered past this: internal (EST)
inline constexpr std::size_t kMaxPending = 256;  // commands polled and not yet answered, across every space (EST)

enum class Status : std::uint8_t {
  ok,
  refused,    // the input fails (with `why`, in words that never quote it)
  duplicate,  // a second space for one mod, or an id another holds
  full,       // kMaxSpaces; or, for poll, the mod's buffer is too small (`length` the size needed; the event is kept)
};

enum class EventKind : std::uint8_t { none = 0, watched = 2, unwatched = 3, command = 4 };  // GRACEUI_EVENT_*

struct Registration {
  std::uint32_t views = 0;      // kViewGame | kViewBrowser
  std::string version;          // "1.2.0", named in welcome
  std::string increment;        // "0.2", named in welcome
  std::string_view pages;       // space_pages JSON (D160): its "space" must be the mod's id
  std::string_view settings;    // settings_description JSON
};

class Registry {
 public:
  using Log = std::function<void(std::string_view)>;
  explicit Registry(Log log = {});
  ~Registry();
  Registry(const Registry&) = delete;
  Registry& operator=(const Registry&) = delete;

  // A mod's space, decoded and checked (its id the mod's own, its views known, its pages and settings agreeing, every
  // default valid), kept for the session; added to the server now if one is attached and it asks for the browser view.
  Status add(std::uint64_t owner, std::string_view mod_id, const Registration& registration, std::string& why);
  // The mod has disconnected: its space leaves the server, and its unanswered commands are dropped.
  void remove_owner(std::uint64_t owner) noexcept;
  // Whether any space asks for the browser view (the server runs while one does, §7.2 item 1).
  bool wants_browser() const noexcept;
  std::size_t count() const noexcept;

  // The server every browser space is added to, once for the session (its object outlives the registry's use of it;
  // the server itself may stop and start again). Spaces registered later join it as they come.
  void attach(serve::Server& server);

  // A state or rows frame of the mod's own space, of one of its declared topics and of that topic's shape, at most the
  // protocol's 256 KiB: ok (dropped if no view watches the topic, or no server runs), or refused with `why`.
  Status publish(std::uint64_t owner, std::string_view frame, std::string& why);
  // The mod's next event, written as a frame into `buffer`: ok with `kind` (none when nothing waits); full, with
  // `length` the size needed and the event kept for the next call, when `capacity` is too small.
  Status poll(std::uint64_t owner, char* buffer, std::uint64_t capacity, EventKind& kind, std::uint64_t& reply,
              std::uint64_t& length);
  // The result frame of the command `reply` names: ok, or refused (not a result, not the mod's command, answered
  // already, or past its deadline). Its space and id are set by the registry, whatever the frame held.
  Status answer(std::uint64_t owner, std::uint64_t reply, std::string_view frame, std::string& why);
  // Commands unanswered past kAnswerDeadline are answered `internal`, one log line each naming the space; answers how
  // many. GraceUI's own thread calls it about once a second while any command waits.
  std::size_t expire(std::chrono::steady_clock::time_point now);
  std::size_t pending() const noexcept;

  // ---- The in-game view's side (D171), for the menu on GraceUI's own thread. The menu watches a topic as a view does:
  // the mod hears WATCHED when the first of the menu and the browser's views watches it, and UNWATCHED when neither
  // does. A state the mod publishes to a topic the menu watches is kept, the newest, for the menu to take. The menu's
  // commands reach the mod as any view's do, with a reply handle and the same deadline; their results wait for it.
  struct GameSpace {
    std::string id;
    std::string name;
    std::string increment;  // as it registered: a page of a later one is absent (D173)
    std::shared_ptr<const protocol::SpacePages> pages;
    std::shared_ptr<const protocol::SettingsDescription> settings;
  };
  struct GameResult {
    std::uint64_t ticket = 0;  // game_command's
    protocol::Result result;
  };
  // Called (outside the lock, from any thread) when something waits for the menu: a space came or went, a state it
  // watches was published, a result of its came back.
  void set_game_wake(std::function<void()> wake);
  // The spaces that asked for the game's view, in the order they registered.
  std::vector<GameSpace> game_spaces() const;
  std::uint64_t spaces_revision() const noexcept;
  void game_watch(std::string_view space, std::string_view topic, bool on);
  // The newest state of a topic the menu watches, if newer than `seen`.
  bool game_state(std::string_view space, std::string_view topic, std::uint64_t seen, protocol::Json& body,
                  std::uint64_t& rev) const;
  // The menu's command to a mod's space: false if the space is not there or too many commands wait; `ticket` names
  // its result.
  bool game_command(std::string_view space, protocol::Cmd command, std::uint64_t& ticket);
  std::vector<GameResult> take_game_results();

 private:
  struct Space;
  struct Pending;
  Space* find(std::uint64_t owner) noexcept;
  const Space* find(std::uint64_t owner) const noexcept;

  Log log_;
  mutable std::mutex mutex_;
  serve::Server* server_ = nullptr;
  std::vector<std::unique_ptr<Space>> spaces_;
  std::vector<Pending> pending_;
  std::uint64_t next_reply_ = 1;
  std::uint64_t spaces_revision_ = 1;
  std::vector<GameResult> game_results_;
  std::function<void()> game_wake_;
  void wake_game() const;
};

}  // namespace grace::spaces
