// internal.h: what grace-serve's I/O files share (server.h): the hub between the server's I/O thread and every other
// thread (the spaces' and the host's), each space's state, and the web files once the worker has read them.
#pragma once

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

#include "grace/protocol/messages.h"
#include "grace/serve/server.h"
#include "grace/serve/space.h"
#include "platform.h"

namespace grace::serve::detail {

using Clock = std::chrono::steady_clock;

// One state topic's newest frame, or one stream topic's ring, as the I/O thread holds them while the topic is watched;
// dropped once nobody watches it (§2.4).
struct TopicCache {
  std::size_t watchers = 0;
  std::uint64_t generation = 0;  // a family member's slot: which member it holds (D215); a state of another is dropped
  // A state's newest revision and its frame, encoded once for every view (D55).
  bool has_state = false;
  std::uint64_t rev = 0;
  std::shared_ptr<const std::string> frame;
  // A stream's rows, oldest first, consecutive from `first_seq`, at most the topic's ring.
  std::deque<protocol::Json> rows;
  std::uint64_t first_seq = 0;
  bool started = false;
};

// A space's slots for its families' members (D215), after its declared topics, each taken by the first view that
// watches a member and given back once nobody watches it and the space has heard so: at most this many members of a
// space are watched at once (EST).
inline constexpr std::size_t kMemberSlots = 64;

// One registered space: its description and what it shares with the I/O thread.
struct PortState {
  SpaceDescription description;
  SpaceWake wake;
  std::size_t declared = 0;  // description.topics.size(): slot `declared + i` is member slot i

  std::mutex mutex;
  // Under `mutex`:
  std::deque<SpaceEvent> commands;     // at most protocol::kCommandQueue (§6.8)
  std::vector<std::uint8_t> watched;   // per topic and member slot, as the I/O thread last set it
  std::vector<std::uint8_t> reported;  // per topic and member slot, as poll last reported it
  struct Member {
    std::string id;                    // the member's topic ("profiles/p1"); empty: the slot is free
    std::uint64_t generation = 0;      // set by the I/O thread when it takes the slot
  };
  std::vector<Member> members;         // kMemberSlots
  std::uint64_t next_generation = 1;

  std::atomic<bool> registered{true};

  // The I/O thread's alone:
  std::vector<TopicCache> topics;
  Clock::time_point full_logged{};

  // The index of a declared topic or command, or npos.
  std::size_t topic_index(std::string_view id) const noexcept;
  bool has_command(std::string_view name) const noexcept;
  // Whether `id` is a member of a family the space declares: "<family>/<key>", the key not empty and with no '/'.
  bool is_member(std::string_view id) const noexcept;
  // A watched member's slot (`declared + i`), or npos; `mutex` held.
  std::size_t member_slot_locked(std::string_view id) const noexcept;
  // A topic's or a member slot's id; `mutex` held for a slot.
  std::string_view topic_id_locked(std::size_t index) const noexcept;
};

inline constexpr std::size_t npos = static_cast<std::size_t>(-1);
// What spaces may have waiting for the I/O thread at once (EST); a publish past it is refused.
inline constexpr std::size_t kMaxInbox = 1024;

// Something a space hands the I/O thread: a state, rows or a result, its frame already encoded.
struct Outgoing {
  enum class Kind : std::uint8_t { state, rows, result };
  Kind kind = Kind::state;
  std::shared_ptr<PortState> port;
  std::size_t topic = 0;
  std::uint64_t rev = 0;
  std::uint64_t from_seq = 0;
  std::uint64_t generation = 0;  // a member's state: its slot's generation when it was published (D215)
  std::vector<protocol::Json> rows;
  std::uint64_t dropped = 0;
  CommandRef reply;
  std::shared_ptr<const std::string> frame;
};

// Between the I/O thread and every other: the spaces registered, what they publish, and the host's requests. One mutex;
// the I/O thread is woken through `wake` while it runs.
struct Hub {
  std::mutex mutex;
  // Under `mutex`:
  std::vector<std::shared_ptr<PortState>> spaces;
  std::deque<Outgoing> inbox;
  bool running = false;
  bool renew = false;
  bool spaces_removed = false;
  const io::Wake* wake = nullptr;
  // Other devices (G9): what the host asked of the network listener, the devices it keeps, the code, the devices paired
  // since it last took them, and the addresses the listener holds, for the host to show.
  bool network_on = false;
  bool network_changed = false;
  std::vector<DeviceKey> devices;
  bool devices_changed = false;
  PairingCode pairing;
  std::vector<PairedDevice> paired;
  std::vector<std::string> network_addresses;
  std::vector<std::string> connected_devices;  // the server's, sorted; the host reads it

  // Wakes the I/O thread; `mutex` must be held.
  void signal_locked() const noexcept {
    if (wake != nullptr) {
      wake->signal();
    }
  }
};

// The web files, read and checked by the worker (files.cpp), then read-only.
struct WebFile {
  std::string name;  // as served, without its leading '/'
  std::string_view type;
  bool html = false;
  std::shared_ptr<const std::string> bytes;
};

struct FileTable {
  bool ok = false;
  std::string failure;  // a fixed text, and a listed file's relative name at most (PV5)
  std::vector<WebFile> files;

  // The file a request's path names, byte for byte (item 21): "/" is index.html.
  const WebFile* find(std::string_view path) const noexcept;
};

// The worker's one job at start (§7.6, item 15): graceui.toml and the web files it lists, or the host's own files.
std::shared_ptr<const FileTable> load_files(const ServerConfig& config) noexcept;

// A WebSocket text frame (unmasked, whole) of a JSON text, and a message as one, encoded once for every view (D55):
// null if it is past the frame's limit.
std::shared_ptr<const std::string> ws_text_frame(std::string_view json);
std::shared_ptr<const std::string> encode_to_page(const protocol::Message& message);

// The notice page (§7.2 item 15, §2.9): built in, plain, within the CSP, naming no path.
std::string_view notice_page() noexcept;

}  // namespace grace::serve::detail
