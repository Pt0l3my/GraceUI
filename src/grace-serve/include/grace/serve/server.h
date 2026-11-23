// server.h: grace-serve, the server itself (G30, §7.2, §7.6): its sockets, its two threads, the web files graceui.toml
// lists, the session cookie and the ticket, and the bridge's routing to each space (§6, D81), as one library that
// graceui.dll and hosts outside the game link alike (G29 (a), §7.2 item 26). It links grace_serve_core for every rule on
// the wire.
//
//   - Listeners on 127.0.0.1 and, where the system offers it, ::1, at the configured port, each bound by name, never the
//     wildcard; SO_EXCLUSIVEADDRUSE on Windows and never SO_REUSEADDR; every socket non-inheritable; a taken port fails
//     closed, with no other port or interface (items 2 and 24).
//   - One non-blocking I/O thread over every socket (WSAPoll, or poll(2)), woken by a wake of its own, never by closing
//     a socket from another thread; one worker for disk reads (§7.6). Each catches every exception at its top; a fault
//     while handling a connection closes that connection, and a second stops the server for the session (§2.3).
//   - Fixed buffers per connection, sized by the caps (D158); 2 WebSockets (a third supersedes the oldest with bye
//     superseded), 16 plain HTTP connections on loopback (a 17th waits in the backlog until one closes), a complete
//     head within 5 s of accept, an absolute deadline, and Connection: close on every response (§6.8, D111).
//   - Routes, all through grace_serve_core's checks: GET of the listed web files; POST /api/ticket, for a one-use ticket;
//     GET /api/ws, the WebSocket upgrade, whose hello must bring the ticket within 2 s of the 101; and the redirect of
//     localhost and [::1] page loads to 127.0.0.1 (items 3, 4, 9, 15, 21 to 23).
//
// Start and stop are ordinary calls, never from DllMain; stop closes every socket and joins both threads, and the
// server can start again. A Server is used from one thread at a time, but add_space, remove_space, renew_session and
// every SpacePort method may be called from any thread.
#pragma once

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "grace/serve/files.h"
#include "grace/serve/pairing.h"
#include "grace/serve/space.h"

namespace grace::serve {

// The default port (D58) and the range a player may choose (§7.2 item 2).
inline constexpr std::uint16_t kDefaultPort = 8642;
inline constexpr std::uint16_t kLowestPort = 1024;

// The routes besides the web files; no web file is listed under /api/.
inline constexpr std::string_view kTicketRoute = "/api/ticket";
inline constexpr std::string_view kSocketRoute = "/api/ws";

// The caps of §6.8 and D111.
inline constexpr std::size_t kMaxWebSockets = 2;
inline constexpr std::size_t kMaxHttpConnections = 16;
// A ticket's POST body: at most this, and its content is not read (item 9).
inline constexpr std::size_t kMaxTicketBody = 256;
// The network listener (G9, item 16): at most this many of the PC's private addresses, and at most this many of the
// plain HTTP connections from them, so that this PC keeps room whatever its home network sends.
inline constexpr std::size_t kMaxNetworkAddresses = 4;
inline constexpr std::size_t kMaxNetworkHttp = 8;

// A device paired on the network listener (G9): its id, the name the player gave it and the hash of its cookie, for
// the host to keep and list; never the cookie.
struct PairedDevice {
  std::string id;
  std::string name;
  std::array<std::uint8_t, 32> hash{};
};
// Tickets fetched and not yet used: a newer one replaces the oldest (EST).
inline constexpr std::size_t kMaxTickets = 8;

// Every deadline of the server, on the monotonic clock. Tests shorten them; a host keeps the defaults.
struct Timings {
  std::chrono::milliseconds head{5000};        // a complete head (and a ticket's body) within this of accept (D111)
  std::chrono::milliseconds hello{2000};       // the hello within this of the 101 (items 4 and 22)
  std::chrono::milliseconds ticket{10000};     // a ticket not used within this of its fetch is stale (EST)
  std::chrono::milliseconds ping{15000};       // a ping this often; two unanswered close the connection (§6.3)
  std::chrono::milliseconds close_wait{1000};  // Close's bounded wait, and the drain after shutdown (items 22, 24)
  std::chrono::milliseconds write{5000};       // an HTTP response must be sent within this (SRV §5.2)
  std::chrono::milliseconds state_interval{250};  // a state topic at most this often to a view (4 Hz, §2.4)
  std::chrono::milliseconds rate_window{1000};    // the window of the 20 messages a connection may send (§6.8)
};

enum class LogLevel : std::uint8_t { info, warning, error };

// One line each, written from the server's threads: fixed words, a port or a count, a listed file's relative name at
// most; never a cookie, a ticket, a payload, a peer's address or a path (PV3, PV5, invariant 5).
using LogSink = std::function<void(LogLevel, std::string_view)>;

struct ServerConfig {
  std::uint16_t port = kDefaultPort;  // 1024 to 65535
  std::string graceui_version;        // welcome's graceui
  std::vector<std::string> caps;      // welcome's caps
  // The web files: GraceUI's folder, holding graceui.toml and web/, read and checked once at start by the worker; or,
  // with from_memory, the files a host hands in.
  std::filesystem::path folder;
  bool from_memory = false;
  std::vector<MemoryFile> memory_files;
  Timings timings;
  // A connection whose frames to send pass this is not reading: it is closed, not buffered for (item 22; EST, two of
  // the largest state frames).
  std::size_t send_queue = 512 * 1024;
  LogSink log;
  // Tests only: the addresses the network listener binds when it is on, in place of this PC's private ones.
  std::vector<std::array<std::uint8_t, 4>> network_addresses;
  // Called on the I/O thread when a device has paired; the host then takes it with take_paired(), on its own thread.
  std::function<void()> paired_wake;
};

enum class StartResult : std::uint8_t {
  ok,
  already_running,
  bad_port,       // outside 1024 to 65535
  port_taken,     // 127.0.0.1:PORT is held by another program (item 2); one log line names both causes
  port_reserved,  // bind refused with access denied: a range of ports Windows reserves (Hyper-V, WSL; item 24)
  no_network,     // the system's sockets could not be started, or a socket made
  no_random,      // the OS random source failed: no cookie, so no server
  no_thread,      // a thread could not be started
};

std::string_view to_string(StartResult result) noexcept;

// What a test or a status page may read of a running server: never a cookie or a ticket.
struct ServerStats {
  bool running = false;
  bool ipv6 = false;           // ::1 is bound too
  std::size_t threads = 0;     // the server's threads alive: 2 while it runs, 0 once stopped
  std::size_t http = 0;        // open plain HTTP connections
  std::size_t websockets = 0;  // WebSockets awaiting their hello or open
  std::size_t sockets = 0;     // every socket the server holds open, listeners and its wake included
  bool files_ready = false;    // the worker has read and checked the web files
  bool files_ok = false;       // and every one passed
  std::size_t faults = 0;
  std::size_t network = 0;             // the network listener's sockets open (G9)
  std::size_t devices = 0;             // devices paired
  std::size_t device_connections = 0;  // connections from paired devices (HTTP and WebSocket)
};

namespace detail {
class Engine;
}

class Server {
 public:
  Server();
  ~Server();  // stops it
  Server(const Server&) = delete;
  Server& operator=(const Server&) = delete;

  StartResult start(const ServerConfig& config);
  // Every WebSocket is told bye shutdown, every socket closed and both threads joined; idempotent.
  void stop() noexcept;
  bool running() const noexcept;

  // A space for views to show: before or after start, from any thread. Its id must be unique among the server's spaces
  // (null otherwise). Views whose welcome came before it do not see it.
  std::shared_ptr<SpacePort> add_space(SpaceDescription description, SpaceWake wake = {});
  // The space is withdrawn (EldenNet's on T10's anti-cheat hit, D81): its port is unregistered, every subscription to
  // it ends, and its topics and commands are unknown to every view from then on.
  void remove_space(std::string_view id) noexcept;

  // dashboard.disconnect (§6.6, §7.2 item 17): a new session cookie, every ticket void, every connection closed (a
  // WebSocket with bye unauthorised, so that its page reloads once for the new cookie). From any thread.
  void renew_session() noexcept;

  // ---- Other devices at home (G9, §7.2 item 16). From any thread; the I/O thread applies them.
  // On, the server also listens on this PC's private IPv4 addresses (at most kMaxNetworkAddresses), at the same port,
  // for paired devices: an unpaired one sees only the pairing page and the code's request. Off, it closes those sockets
  // and every connection from them. A host turns it on only while its player has switched it on.
  void set_network(bool on) noexcept;
  // The addresses it listens on now, dotted, for the game to show; empty while it is off or found none.
  std::vector<std::string> network_addresses() const;
  // A new pairing code, for kCodeLife, replacing any shown: for the game to show, never a view (it is not published);
  // empty if the system gave no random bytes.
  std::string start_pairing();
  void cancel_pairing() noexcept;
  // The code showing now, or empty.
  std::string pairing_code() const;
  // The paired devices the host keeps (from its file, after a Forget), replacing the server's: a device no longer
  // listed is disconnected at once, and its cookie is good for nothing. A device paired since the host last took it is
  // kept whatever the list says.
  void set_devices(std::vector<DeviceKey> devices) noexcept;
  // The devices paired since the last call, oldest first.
  std::vector<PairedDevice> take_paired();

  ServerStats stats() const noexcept;

 private:
  std::shared_ptr<detail::Hub> hub_;
  std::unique_ptr<detail::Engine> engine_;
};

}  // namespace grace::serve
