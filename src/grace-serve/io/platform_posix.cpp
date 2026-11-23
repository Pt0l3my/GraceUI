// platform_posix.cpp: platform.h on Linux, for the unit tests and the security suite over real sockets (§9.1), and for
// a host outside the game on Linux. Every descriptor is made with CLOEXEC (the POSIX form of non-inheritable) and
// non-blocking; poll(2) waits; an eventfd wakes; getrandom gives the cookie's bytes.
#include <array>
#include <cerrno>
#include <cstring>

#include <arpa/inet.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <netinet/in.h>
#include <poll.h>
#include <pthread.h>
#include <sys/eventfd.h>
#include <sys/random.h>
#include <sys/socket.h>
#include <unistd.h>

#include "platform.h"

namespace grace::serve::io {
namespace {

// Every address the server handles, in one place: the system reads and writes it through `any`, and the server reads
// it back by copying its bytes out (no cast between the address types).
union Address {
  sockaddr any;
  sockaddr_in v4;
  sockaddr_in6 v6;
  sockaddr_storage storage;
};

bool set_int_option(Handle handle, int level, int name, int value) noexcept {
  return setsockopt(handle, level, name, &value, sizeof value) == 0;
}

BindError bind_error(int code) noexcept {
  switch (code) {
    case EADDRINUSE: return BindError::in_use;
    case EACCES:
    case EPERM: return BindError::denied;
    case EADDRNOTAVAIL:
    case EAFNOSUPPORT:
    case EPROTONOSUPPORT: return BindError::unavailable;
    default: return BindError::other;
  }
}

void peer_of(const Address& address, Accepted& out) noexcept {
  sa_family_t family{};
  std::memcpy(&family, &address, sizeof family);
  if (family == AF_INET) {
    sockaddr_in v4{};
    std::memcpy(&v4, &address, sizeof v4);
    const std::uint32_t host = ntohl(v4.sin_addr.s_addr);
    out.loopback = (host >> 24) == 127;
    out.exact = host == 0x7F000001u;
    out.home = (host >> 24) == 10 || (host >> 20) == 0xAC1u || (host >> 16) == 0xC0A8u || (host >> 16) == 0xA9FEu;
    out.ipv4 = host;
  } else if (family == AF_INET6) {
    sockaddr_in6 v6{};
    std::memcpy(&v6, &address, sizeof v6);
    std::array<std::uint8_t, 16> bytes{};
    std::memcpy(bytes.data(), &v6.sin6_addr, bytes.size());
    bool loopback = bytes[15] == 1;
    for (std::size_t i = 0; i < 15; ++i) {
      loopback = loopback && bytes[i] == 0;
    }
    out.loopback = loopback;
    out.exact = loopback;
  }
}

}  // namespace

Socket& Socket::operator=(Socket&& other) noexcept {
  if (this != &other) {
    close();
    handle_ = other.handle_;
    other.handle_ = kNoHandle;
  }
  return *this;
}

void Socket::close() noexcept {
  if (handle_ != kNoHandle) {
    ::close(handle_);
    handle_ = kNoHandle;
  }
}

bool start_network() noexcept { return true; }
void stop_network() noexcept {}

Socket listen_loopback(bool ipv6, std::uint16_t port, BindError& error) noexcept {
  error = BindError::none;
  Socket socket(::socket(ipv6 ? AF_INET6 : AF_INET, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0));
  if (!socket.open()) {
    error = bind_error(errno);
    return {};
  }
  Address address{};
  socklen_t size = 0;
  if (ipv6) {
    // ::1 alone: never a v4-mapped address through this socket.
    if (!set_int_option(socket.handle(), IPPROTO_IPV6, IPV6_V6ONLY, 1)) {
      error = BindError::other;
      return {};
    }
    address.v6.sin6_family = AF_INET6;
    address.v6.sin6_port = htons(port);
    address.v6.sin6_addr = in6addr_loopback;
    size = sizeof address.v6;
  } else {
    address.v4.sin_family = AF_INET;
    address.v4.sin_port = htons(port);
    address.v4.sin_addr.s_addr = htonl(0x7F000001u);
    size = sizeof address.v4;
  }
  // No SO_REUSEADDR (item 24): a port with a listener, or one still in TIME_WAIT, is taken.
  if (::bind(socket.handle(), &address.any, size) != 0) {
    error = bind_error(errno);
    return {};
  }
  if (::listen(socket.handle(), 64) != 0) {
    error = bind_error(errno);
    return {};
  }
  return socket;
}

std::uint16_t local_port(const Socket& socket) noexcept {
  Address address{};
  socklen_t size = sizeof address;
  if (::getsockname(socket.handle(), &address.any, &size) != 0) {
    return 0;
  }
  return address.any.sa_family == AF_INET    ? ntohs(address.v4.sin_port)
         : address.any.sa_family == AF_INET6 ? ntohs(address.v6.sin6_port)
                                             : 0;
}

Socket listen_ipv4(std::array<std::uint8_t, 4> ip, std::uint16_t port, BindError& error) noexcept {
  error = BindError::none;
  Socket socket(::socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0));
  if (!socket.open()) {
    error = bind_error(errno);
    return {};
  }
  Address address{};
  address.v4.sin_family = AF_INET;
  address.v4.sin_port = htons(port);
  std::memcpy(&address.v4.sin_addr.s_addr, ip.data(), 4);  // already in network order
  if (::bind(socket.handle(), &address.any, sizeof address.v4) != 0 || ::listen(socket.handle(), 64) != 0) {
    error = bind_error(errno);
    return {};
  }
  return socket;
}

std::size_t local_ipv4_addresses(std::span<std::array<std::uint8_t, 4>> out) noexcept {
  ifaddrs* list = nullptr;
  if (::getifaddrs(&list) != 0) {
    return 0;
  }
  std::size_t count = 0;
  for (const ifaddrs* at = list; at != nullptr && count < out.size(); at = at->ifa_next) {
    if (at->ifa_addr == nullptr || at->ifa_addr->sa_family != AF_INET || (at->ifa_flags & IFF_UP) == 0) {
      continue;
    }
    sockaddr_in v4{};
    std::memcpy(&v4, at->ifa_addr, sizeof v4);
    if (v4.sin_addr.s_addr == 0) {
      continue;  // no address yet (0.0.0.0)
    }
    std::memcpy(out[count++].data(), &v4.sin_addr.s_addr, 4);
  }
  ::freeifaddrs(list);
  return count;
}

Io accept_one(const Socket& listener, Accepted& out) noexcept {
  Address address{};
  socklen_t size = sizeof address;
  const Handle handle = ::accept4(listener.handle(), &address.any, &size, SOCK_NONBLOCK | SOCK_CLOEXEC);
  if (handle < 0) {
    return errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR || errno == ECONNABORTED ? Io::would_block
                                                                                              : Io::failed;
  }
  out.socket = Socket(handle);
  out.loopback = false;
  out.exact = false;
  peer_of(address, out);
  return Io::done;
}

Io receive(const Socket& socket, std::span<char> buffer, std::size_t& count) noexcept {
  count = 0;
  const ssize_t got = ::recv(socket.handle(), buffer.data(), buffer.size(), 0);
  if (got > 0) {
    count = static_cast<std::size_t>(got);
    return Io::done;
  }
  if (got == 0) {
    return Io::closed;
  }
  return errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR ? Io::would_block : Io::failed;
}

Io transmit(const Socket& socket, std::span<const char> bytes, std::size_t& count) noexcept {
  count = 0;
  // MSG_NOSIGNAL: a peer gone is an error to handle, never a SIGPIPE in the host's process.
  const ssize_t sent = ::send(socket.handle(), bytes.data(), bytes.size(), MSG_NOSIGNAL);
  if (sent >= 0) {
    count = static_cast<std::size_t>(sent);
    return Io::done;
  }
  return errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR ? Io::would_block : Io::failed;
}

void shutdown_send(const Socket& socket) noexcept { ::shutdown(socket.handle(), SHUT_WR); }

bool Wake::create() noexcept {
  close();
  handle_ = ::eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
  return handle_ != kNoHandle;
}

void Wake::close() noexcept {
  if (handle_ != kNoHandle) {
    ::close(handle_);
    handle_ = kNoHandle;
  }
}

void Wake::signal() const noexcept {
  const std::uint64_t one = 1;
  // A full counter (never, in practice) still leaves it readable, which is all a wake needs.
  [[maybe_unused]] const ssize_t written = ::write(handle_, &one, sizeof one);
}

void Wake::drain() const noexcept {
  std::uint64_t value = 0;
  [[maybe_unused]] const ssize_t got = ::read(handle_, &value, sizeof value);
}

bool poll_all(std::span<PollItem> items, int timeout_ms) noexcept {
  if (items.size() > kMaxPollItems) {
    return false;
  }
  std::array<pollfd, kMaxPollItems> native{};
  for (std::size_t i = 0; i < items.size(); ++i) {
    native[i].fd = items[i].handle;
    native[i].events = static_cast<short>((items[i].read ? POLLIN : 0) | (items[i].write ? POLLOUT : 0));
  }
  const int ready = ::poll(native.data(), static_cast<nfds_t>(items.size()), timeout_ms);
  if (ready < 0) {
    return errno == EINTR;
  }
  for (std::size_t i = 0; i < items.size(); ++i) {
    const short events = native[i].revents;
    items[i].readable = (events & POLLIN) != 0;
    items[i].writable = (events & POLLOUT) != 0;
    items[i].failed = (events & (POLLERR | POLLHUP | POLLNVAL)) != 0;
  }
  return true;
}

bool random_bytes(std::span<std::uint8_t> out) noexcept {
  std::size_t filled = 0;
  while (filled < out.size()) {
    const ssize_t got = ::getrandom(out.data() + filled, out.size() - filled, 0);
    if (got < 0) {
      if (errno == EINTR) {
        continue;
      }
      return false;
    }
    filled += static_cast<std::size_t>(got);
  }
  return true;
}

void name_thread(const char* name) noexcept { ::pthread_setname_np(::pthread_self(), name); }

}  // namespace grace::serve::io
