// platform.h: the little of the system grace-serve needs (§7.2 items 2, 3 and 24; §7.6), behind one interface with two
// implementations: Winsock (platform_win32.cpp, WSAPoll) and POSIX (platform_posix.cpp, poll(2)). No other file of the
// server includes a system header for sockets, threads' names or random bytes.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace grace::serve::io {

#if defined(_WIN32)
using Handle = std::uintptr_t;  // a SOCKET
inline constexpr Handle kNoHandle = ~Handle{0};
#else
using Handle = int;
inline constexpr Handle kNoHandle = -1;
#endif

// One socket, closed when it goes (by the thread that owns it, never from another to wake it, §7.6).
class Socket {
 public:
  Socket() noexcept = default;
  explicit Socket(Handle handle) noexcept : handle_(handle) {}
  Socket(Socket&& other) noexcept : handle_(other.handle_) { other.handle_ = kNoHandle; }
  Socket& operator=(Socket&& other) noexcept;
  Socket(const Socket&) = delete;
  Socket& operator=(const Socket&) = delete;
  ~Socket() { close(); }

  Handle handle() const noexcept { return handle_; }
  bool open() const noexcept { return handle_ != kNoHandle; }
  void close() noexcept;

 private:
  Handle handle_ = kNoHandle;
};

// WSAStartup and WSACleanup on Windows; nothing elsewhere. Called by start and stop, never from DllMain.
bool start_network() noexcept;
void stop_network() noexcept;

enum class BindError : std::uint8_t {
  none,
  in_use,       // another socket holds the address and port
  denied,       // access denied: on Windows, a reserved range of ports (Hyper-V, WSL)
  unavailable,  // the address does not exist here (no IPv6)
  other,
};

// A listening socket on 127.0.0.1 or ::1 (IPv6 only) at `port`: bound by name, SO_EXCLUSIVEADDRUSE on Windows and
// never SO_REUSEADDR, non-inheritable and non-blocking (item 24).
Socket listen_loopback(bool ipv6, std::uint16_t port, BindError& error) noexcept;

// A listening socket on one IPv4 address of this PC at `port` (the network listener, G9, §7.2 item 16): bound by that
// address, never the wildcard, with the same options as the loopback's.
Socket listen_ipv4(std::array<std::uint8_t, 4> address, std::uint16_t port, BindError& error) noexcept;

// The port a bound socket holds (the system's choice for one bound at port 0); 0 if it cannot be read.
std::uint16_t local_port(const Socket& socket) noexcept;

// The IPv4 addresses of this PC's interfaces that are up, in the order the system lists them, at most out.size(), never
// 0.0.0.0 (an adapter with none yet, as Wine lists them); the
// caller keeps the private ones (pairing.h). Through Winsock on Windows (no import of its own), getifaddrs elsewhere.
std::size_t local_ipv4_addresses(std::span<std::array<std::uint8_t, 4>> out) noexcept;

struct Accepted {
  Socket socket;
  bool loopback = false;  // the peer is a loopback address (127.0.0.0/8 or ::1)
  bool exact = false;     // the peer is exactly 127.0.0.1 or ::1 (item 3)
  bool home = false;      // a private (RFC 1918) or link-local (169.254/16) IPv4 peer: another device at home (item 25)
  std::uint32_t ipv4 = 0;  // an IPv4 peer's address as a number (0 for IPv6): a pairing code's tries per peer (G9)
};

enum class Io : std::uint8_t { done, would_block, closed, failed };

// One connection taken from the listener's queue, non-inheritable and non-blocking.
Io accept_one(const Socket& listener, Accepted& out) noexcept;

// Bytes received or sent: `count` is set when done; closed is the peer's orderly end (receive only).
Io receive(const Socket& socket, std::span<char> buffer, std::size_t& count) noexcept;
Io transmit(const Socket& socket, std::span<const char> bytes, std::size_t& count) noexcept;
// The send side ended (FIN), the receive side drained by the caller within a bounded wait (item 24).
void shutdown_send(const Socket& socket) noexcept;

// The I/O thread's own wake (§7.6): any thread may wake it; only the I/O thread drains it, and it is closed once that
// thread has been joined. An eventfd on Linux, a UDP socket on 127.0.0.1 connected to itself on Windows.
class Wake {
 public:
  Wake() noexcept = default;
  Wake(const Wake&) = delete;
  Wake& operator=(const Wake&) = delete;
  ~Wake() { close(); }

  bool create() noexcept;
  void close() noexcept;
  void signal() const noexcept;
  void drain() const noexcept;
  Handle handle() const noexcept { return handle_; }

 private:
  Handle handle_ = kNoHandle;
};

struct PollItem {
  Handle handle = kNoHandle;
  bool read = false;
  bool write = false;
  // Set by poll_all:
  bool readable = false;
  bool writable = false;
  bool failed = false;  // an error or a hang-up: read it to see which
};

inline constexpr std::size_t kMaxPollItems = 40;

// Waits for any item, at most `timeout_ms`: false only if the system's poll failed. At most kMaxPollItems.
bool poll_all(std::span<PollItem> items, int timeout_ms) noexcept;

// Random bytes from the OS (BCryptGenRandom, getrandom): false if it could not give them all.
bool random_bytes(std::span<std::uint8_t> out) noexcept;

// The calling thread's name, for crash dumps (§7.6): at most 15 characters.
void name_thread(const char* name) noexcept;

}  // namespace grace::serve::io
