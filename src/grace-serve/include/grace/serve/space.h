// space.h: how a host gives grace-serve its spaces (§2.9, §6.1, D81), and how each space hears from the views and
// answers them. A host (graceui.dll for the spaces its consumers register, grace-dev for fixtures, EldenNet's launcher
// off game, G29) calls Server::add_space with a SpaceDescription and gets a SpacePort, which the space then uses from a
// thread of its own, as the ABI's poll, publish and answer are used (§6.2):
//
//   - poll: what the views want of the space, one event at a time, never waiting: a topic now watched by at least one
//     view, a topic no longer watched by any (so that a space builds nothing nobody watches, §2.4), and the commands the
//     views sent it, each the space's own (declared) and still for its one validating entry point to check (invariant
//     3). The commands wait in a queue of 32 per space (§6.8); when it is full the newest is dropped, its view told
//     rate_limited, and one log line written, so nothing ever blocks.
//   - publish: a state (its whole body and revision) or stream rows of one of its declared topics, encoded once and
//     sent to every view that watches it (§2.4, D55); a state at most 4 times a second to each view, the newest.
//   - answer: the result of one command, sent to the view that asked it and to no other.
//
// The server routes nothing across spaces: a view reaches only the spaces its welcome named, and a topic or command a
// space did not declare is answered unknown_topic or unknown_command without reaching it (invariant 3, D81). Every
// method of SpacePort is thread-safe and noexcept; the event callback runs on the server's I/O thread and must return
// at once.
#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "grace/protocol/messages.h"

namespace grace::serve {

// What the server knows of a space: what welcome names (its id, name, version, increment and settings schema), the
// topics it publishes and the commands it takes, as its page data declares them (D160).
struct SpaceDescription {
  protocol::SpaceInfo info;
  std::vector<protocol::TopicInfo> topics;
  std::vector<std::string> commands;
  // The commands a destructive action of its pages sends: the server refuses one whose args lack `"confirm": true`
  // with confirm_required, and the space never sees it (§7.3, D173).
  std::vector<std::string> destructive;
  // The space's page data (space_pages), which the framework's own topic "pages" carries to a view that subscribes to
  // it: the server answers that sub itself, from this, and the space never sees it (D166, D169). Null: no such topic.
  protocol::Json pages;
};

// The framework's topic that carries a space's page data to a view (D166): every space has it, answered by the server.
inline constexpr std::string_view kPagesTopic = "pages";

// A description from a space's page data and settings description, as grace-pages and a mod's register_pages give them.
SpaceDescription describe_space(const protocol::SpacePages& pages, const protocol::SettingsDescription& settings,
                                std::string version, std::string increment);

// Whether a command may reach its space: one no destructive action sends, or one whose args hold "confirm": true
// (§7.3, D173). Every view's commands are held to it, the in-game menu's too.
bool confirmed(const SpaceDescription& description, std::string_view command, const protocol::Json& args) noexcept;

// Where a command's result goes: the connection that sent it (never taken from a payload, §7.2 item 11) and its id.
struct CommandRef {
  std::uint64_t connection = 0;
  std::uint32_t id = 0;
};

struct SpaceEvent {
  enum class Kind : std::uint8_t {
    none,       // nothing is waiting
    watched,    // `topic` is now watched by a view: publish it (a stream from its oldest row still held)
    unwatched,  // `topic` is watched by no view: stop building it
    command,    // `command` with `args`, for the space to validate and apply, then answer(reply, ...)
  };
  Kind kind = Kind::none;
  std::string topic;
  std::string command;
  protocol::Json args;
  CommandRef reply;
};

namespace detail {
struct Hub;
struct PortState;
}  // namespace detail

class SpacePort {
 public:
  SpacePort(const SpacePort&) = delete;
  SpacePort& operator=(const SpacePort&) = delete;
  ~SpacePort();

  const SpaceDescription& description() const noexcept;

  // The next event at once: false (and Kind::none) when nothing is waiting. Changes of watching come before commands,
  // and a topic watched and unwatched again before a poll is not reported at all.
  bool poll(SpaceEvent& event) noexcept;

  // A protocol::State or protocol::Rows of one of this space's declared topics, of its shape (its space field is set
  // to this space's id, whatever it held): false if it is neither, names another topic or shape, is past the frame's
  // limit (256 KiB, or 100 rows), or the space was removed. Taken only while the topic is watched; once it is not, it
  // is dropped. A stream's rows follow on from the last ones (from_seq the next sequence number, or a gap, which
  // starts the stream over there); an empty batch past a gap clears the stream in every view (protocol 1.1).
  bool publish(protocol::Message message) noexcept;

  // The result of one command: ok, or an error code, with an optional body. False if it is both or neither, or the
  // space was removed; a result for a view that has gone is dropped.
  bool answer(const CommandRef& reply, protocol::Result result) noexcept;

  // False once the space has been removed from its server.
  bool registered() const noexcept;

 private:
  friend class Server;
  SpacePort(std::shared_ptr<detail::Hub> hub, std::shared_ptr<detail::PortState> state) noexcept;

  std::shared_ptr<detail::Hub> hub_;
  std::shared_ptr<detail::PortState> state_;
};

// Called on the server's I/O thread whenever an event is queued for the space: a host wakes the space's thread with it.
using SpaceWake = std::function<void()>;

}  // namespace grace::serve
