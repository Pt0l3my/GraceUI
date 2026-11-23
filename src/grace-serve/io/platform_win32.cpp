// platform_win32.cpp: platform.h on Windows (graceui.dll, and a host outside the game such as EldenNet's launcher):
// Winsock 2 with WSAPoll, sockets made with WSA_FLAG_NO_HANDLE_INHERIT and an accepted one's handle made
// non-inheritable, SO_EXCLUSIVEADDRUSE before bind and never SO_REUSEADDR (item 24), a UDP socket on 127.0.0.1
// connected to itself as the I/O thread's wake, and BCryptGenRandom for the cookie's bytes (item 3).
//
// SAFETY: this file holds the server's one cast, a SOCKET (an integer) to the HANDLE that SetHandleInformation takes;
// a SOCKET is a kernel handle by Winsock's own contract, so the value names the same object. Addresses go through a
// union and are copied out by bytes, so no pointer is ever cast.
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <bcrypt.h>

#include <array>
#include <cstring>

#include "platform.h"

#if defined(__MINGW32__)
// MinGW-w64's headers do not declare it (Windows 10 1607 and later, Wine 9); its kernel32 import library has it.
extern "C" __declspec(dllimport) HRESULT WINAPI SetThreadDescription(HANDLE thread, PCWSTR description);
#endif

namespace grace::serve::io {
namespace {

static_assert(sizeof(Handle) == sizeof(SOCKET), "a Handle holds a SOCKET");

union Address {
  sockaddr any;
  sockaddr_in v4;
  sockaddr_in6 v6;
  sockaddr_storage storage;
};

// setsockopt takes its value as bytes.
bool set_int_option(SOCKET socket, int level, int name, int value) noexcept {
  std::array<char, sizeof(int)> bytes{};
  std::memcpy(bytes.data(), &value, bytes.size());
  return ::setsockopt(socket, level, name, bytes.data(), static_cast<int>(bytes.size())) == 0;
}

bool set_non_blocking(SOCKET socket) noexcept {
  u_long on = 1;
  return ::ioctlsocket(socket, static_cast<long>(0x8004667Eu) /* FIONBIO */, &on) == 0;
}

bool not_inherited(SOCKET socket) noexcept {
  // SAFETY: a SOCKET is a kernel handle (Winsock's contract), so this names the same object; nothing is dereferenced.
  return ::SetHandleInformation(reinterpret_cast<HANDLE>(socket), HANDLE_FLAG_INHERIT, 0) != 0;
}

SOCKET make_socket(int family, int type, int protocol) noexcept {
  const SOCKET socket = ::WSASocketW(family, type, protocol, nullptr, 0, WSA_FLAG_OVERLAPPED | WSA_FLAG_NO_HANDLE_INHERIT);
  if (socket == ~SOCKET{0}) {
    return socket;
  }
  if (!set_non_blocking(socket)) {
    ::closesocket(socket);
    return ~SOCKET{0};
  }
  return socket;
}

BindError bind_error(int code) noexcept {
  switch (code) {
    case WSAEADDRINUSE: return BindError::in_use;
    case WSAEACCES: return BindError::denied;
    case WSAEADDRNOTAVAIL:
    case WSAEAFNOSUPPORT:
    case WSAEPROTONOSUPPORT:
    case WSAEPFNOSUPPORT: return BindError::unavailable;
    default: return BindError::other;
  }
}

bool would_block(int code) noexcept { return code == WSAEWOULDBLOCK || code == WSAEINTR || code == WSAEINPROGRESS; }

void peer_of(const Address& address, Accepted& out) noexcept {
  ADDRESS_FAMILY family{};
  std::memcpy(&family, &address, sizeof family);
  if (family == AF_INET) {
    sockaddr_in v4{};
    std::memcpy(&v4, &address, sizeof v4);
    const std::uint32_t host = ntohl(v4.sin_addr.s_addr);
    out.loopback = (host >> 24) == 127;
    out.exact = host == 0x7F000001u;
    out.home = (host >> 24) == 10 || (host >> 20) == 0xAC1u || (host >> 16) == 0xC0A8u || (host >> 16) == 0xA9FEu;
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
    ::closesocket(handle_);
    handle_ = kNoHandle;
  }
}

bool start_network() noexcept {
  WSADATA data{};
  return ::WSAStartup(MAKEWORD(2, 2), &data) == 0;
}

void stop_network() noexcept { ::WSACleanup(); }

Socket listen_loopback(bool ipv6, std::uint16_t port, BindError& error) noexcept {
  error = BindError::none;
  Socket socket(make_socket(ipv6 ? AF_INET6 : AF_INET, SOCK_STREAM, IPPROTO_TCP));
  if (!socket.open()) {
    error = bind_error(::WSAGetLastError());
    return {};
  }
  // Before bind: no other socket may then be bound to this address and port, as SO_REUSEADDR would let one (item 24).
  if (!set_int_option(socket.handle(), SOL_SOCKET, SO_EXCLUSIVEADDRUSE, 1)) {
    error = BindError::other;
    return {};
  }
  Address address{};
  int size = 0;
  if (ipv6) {
    if (!set_int_option(socket.handle(), IPPROTO_IPV6, IPV6_V6ONLY, 1)) {
      error = BindError::other;
      return {};
    }
    address.v6.sin6_family = AF_INET6;
    address.v6.sin6_port = ::htons(port);
    address.v6.sin6_addr.s6_addr[15] = 1;  // ::1
    size = sizeof address.v6;
  } else {
    address.v4.sin_family = AF_INET;
    address.v4.sin_port = ::htons(port);
    address.v4.sin_addr.s_addr = ::htonl(0x7F000001u);
    size = sizeof address.v4;
  }
  if (::bind(socket.handle(), &address.any, size) != 0 || ::listen(socket.handle(), 64) != 0) {
    error = bind_error(::WSAGetLastError());
    return {};
  }
  return socket;
}

Socket listen_ipv4(std::array<std::uint8_t, 4> ip, std::uint16_t port, BindError& error) noexcept {
  error = BindError::none;
  Socket socket(make_socket(AF_INET, SOCK_STREAM, IPPROTO_TCP));
  if (!socket.open()) {
    error = bind_error(::WSAGetLastError());
    return {};
  }
  if (!set_int_option(socket.handle(), SOL_SOCKET, SO_EXCLUSIVEADDRUSE, 1)) {
    error = BindError::other;
    return {};
  }
  Address address{};
  address.v4.sin_family = AF_INET;
  address.v4.sin_port = ::htons(port);
  std::memcpy(&address.v4.sin_addr.s_addr, ip.data(), 4);  // already in network order
  if (::bind(socket.handle(), &address.any, sizeof address.v4) != 0 || ::listen(socket.handle(), 64) != 0) {
    error = bind_error(::WSAGetLastError());
    return {};
  }
  return socket;
}

std::size_t local_ipv4_addresses(std::span<std::array<std::uint8_t, 4>> out) noexcept {
  // SIO_ADDRESS_LIST_QUERY on a socket of the family: Winsock's own list, so graceui.dll imports nothing new.
  Socket probe(make_socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP));
  if (!probe.open()) {
    return 0;
  }
  alignas(SOCKET_ADDRESS_LIST) std::array<char, 4096> buffer{};
  DWORD returned = 0;
  if (::WSAIoctl(probe.handle(), SIO_ADDRESS_LIST_QUERY, nullptr, 0, buffer.data(), static_cast<DWORD>(buffer.size()),
                 &returned, nullptr, nullptr) != 0) {
    return 0;
  }
  // SAFETY: on success Winsock wrote a SOCKET_ADDRESS_LIST at the start of the buffer, its addresses within it.
  const auto* list = reinterpret_cast<const SOCKET_ADDRESS_LIST*>(buffer.data());
  std::size_t count = 0;
  for (INT i = 0; i < list->iAddressCount && count < out.size(); ++i) {
    const SOCKET_ADDRESS& entry = list->Address[i];
    if (entry.lpSockaddr == nullptr || entry.lpSockaddr->sa_family != AF_INET ||
        entry.iSockaddrLength < static_cast<INT>(sizeof(sockaddr_in))) {
      continue;
    }
    sockaddr_in v4{};
    std::memcpy(&v4, entry.lpSockaddr, sizeof v4);
    if (v4.sin_addr.s_addr == 0) {
      continue;  // an adapter with no address yet (0.0.0.0) is no address of the PC's
    }
    std::memcpy(out[count++].data(), &v4.sin_addr.s_addr, 4);
  }
  return count;
}

Io accept_one(const Socket& listener, Accepted& out) noexcept {
  Address address{};
  int size = sizeof address;
  const SOCKET socket = ::accept(listener.handle(), &address.any, &size);
  if (socket == ~SOCKET{0}) {
    const int code = ::WSAGetLastError();
    return would_block(code) || code == WSAECONNRESET ? Io::would_block : Io::failed;
  }
  out.socket = Socket(socket);
  out.loopback = false;
  out.exact = false;
  if (!set_non_blocking(socket) || !not_inherited(socket)) {
    out.socket.close();
    return Io::would_block;
  }
  peer_of(address, out);
  return Io::done;
}

Io receive(const Socket& socket, std::span<char> buffer, std::size_t& count) noexcept {
  count = 0;
  const int want = static_cast<int>(buffer.size() < 0x40000000u ? buffer.size() : 0x40000000u);
  const int got = ::recv(socket.handle(), buffer.data(), want, 0);
  if (got > 0) {
    count = static_cast<std::size_t>(got);
    return Io::done;
  }
  if (got == 0) {
    return Io::closed;
  }
  return would_block(::WSAGetLastError()) ? Io::would_block : Io::failed;
}

Io transmit(const Socket& socket, std::span<const char> bytes, std::size_t& count) noexcept {
  count = 0;
  const int want = static_cast<int>(bytes.size() < 0x40000000u ? bytes.size() : 0x40000000u);
  const int sent = ::send(socket.handle(), bytes.data(), want, 0);
  if (sent >= 0) {
    count = static_cast<std::size_t>(sent);
    return Io::done;
  }
  return would_block(::WSAGetLastError()) ? Io::would_block : Io::failed;
}

void shutdown_send(const Socket& socket) noexcept { ::shutdown(socket.handle(), SD_SEND); }

bool Wake::create() noexcept {
  close();
  const SOCKET socket = make_socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
  if (socket == ~SOCKET{0}) {
    return false;
  }
  handle_ = socket;
  Address address{};
  address.v4.sin_family = AF_INET;
  address.v4.sin_addr.s_addr = ::htonl(0x7F000001u);
  int size = sizeof address.v4;
  // Bound to 127.0.0.1 at a port the system picks, then connected to itself: it takes datagrams from itself alone.
  if (::bind(socket, &address.any, size) != 0 || ::getsockname(socket, &address.any, &size) != 0 ||
      ::connect(socket, &address.any, size) != 0) {
    close();
    return false;
  }
  return true;
}

void Wake::close() noexcept {
  if (handle_ != kNoHandle) {
    ::closesocket(handle_);
    handle_ = kNoHandle;
  }
}

void Wake::signal() const noexcept {
  const char byte = 1;
  ::send(handle_, &byte, 1, 0);
}

void Wake::drain() const noexcept {
  std::array<char, 64> bytes{};
  while (::recv(handle_, bytes.data(), static_cast<int>(bytes.size()), 0) > 0) {
  }
}

bool poll_all(std::span<PollItem> items, int timeout_ms) noexcept {
  if (items.size() > kMaxPollItems) {
    return false;
  }
  std::array<WSAPOLLFD, kMaxPollItems> native{};
  for (std::size_t i = 0; i < items.size(); ++i) {
    native[i].fd = items[i].handle;
    // WSAPoll takes the normal-data flags alone (POLLPRI and the band flags are refused).
    native[i].events = static_cast<SHORT>((items[i].read ? POLLRDNORM : 0) | (items[i].write ? POLLWRNORM : 0));
  }
  const int ready = ::WSAPoll(native.data(), static_cast<ULONG>(items.size()), timeout_ms);
  if (ready == SOCKET_ERROR) {
    return false;
  }
  for (std::size_t i = 0; i < items.size(); ++i) {
    const SHORT events = native[i].revents;
    items[i].readable = (events & (POLLRDNORM | POLLHUP)) != 0;
    items[i].writable = (events & POLLWRNORM) != 0;
    items[i].failed = (events & (POLLERR | POLLHUP | POLLNVAL)) != 0;
  }
  return true;
}

bool random_bytes(std::span<std::uint8_t> out) noexcept {
  if (out.size() > 0xFFFFFFFFu) {
    return false;
  }
  return ::BCryptGenRandom(nullptr, out.data(), static_cast<ULONG>(out.size()), BCRYPT_USE_SYSTEM_PREFERRED_RNG) == 0;
}

void name_thread(const char* name) noexcept {
  std::array<wchar_t, 16> wide{};
  for (std::size_t i = 0; i + 1 < wide.size() && name[i] != '\0'; ++i) {
    wide[i] = static_cast<wchar_t>(static_cast<unsigned char>(name[i]));
  }
  ::SetThreadDescription(::GetCurrentThread(), wide.data());
}

}  // namespace grace::serve::io
