// pairing.h: another device at home, paired once (G9, §7.2 item 16): which of the PC's addresses the network listener
// may take, the six-digit code the game shows, the paired devices as the hash of their cookie, and the pairing page an
// unpaired device sees. Pure: no sockets, no clocks read, no allocation; the I/O part (io/server.cpp) gives it the time
// and the random bytes.
#pragma once

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

#include "grace/serve/limits.h"

namespace grace::serve {

// A paired device's cookie (HttpOnly; SameSite=Strict; host-only), on the network listener alone.
inline constexpr std::string_view kDeviceCookie = "en_device";
// The code's request, and the pairing page's own script and style (CSP: no inline script, no form posts).
inline constexpr std::string_view kPairRoute = "/api/pair";
inline constexpr std::string_view kPairScriptRoute = "/pair.js";
inline constexpr std::string_view kPairStyleRoute = "/pair.css";

inline constexpr std::size_t kCodeDigits = 6;
inline constexpr std::size_t kCodeTries = 5;  // wrong tries before the code is void
inline constexpr std::chrono::seconds kCodeLife{120};
inline constexpr std::size_t kMaxDevices = 16;     // paired at once (EST)
inline constexpr std::size_t kMaxDeviceName = 32;  // characters
inline constexpr std::size_t kDeviceIdChars = 16;  // lowercase hex of 8 random bytes
inline constexpr std::size_t kMaxPairBody = 256;   // the code's request body
// A device cookie's life in the browser: as long as browsers keep one (400 days), renewed at each page load.
inline constexpr std::uint32_t kDeviceCookieSeconds = 400u * 24u * 3600u;

// One of the PC's private IPv4 addresses (RFC 1918: 10/8, 172.16/12, 192.168/16); never a public, loopback,
// link-local or shared address (§7.2 item 16).
bool private_ipv4(std::span<const std::uint8_t, 4> address) noexcept;
// Its dotted form (four decimal numbers and three dots), as a Host names it: the characters written, at most 15.
std::size_t format_ipv4(std::span<const std::uint8_t, 4> address, std::span<char, 16> out) noexcept;

// A paired device as the server holds it: its id and the SHA-256 of its cookie, never the cookie (§7.2 item 16).
struct DeviceKey {
  std::array<char, kDeviceIdChars> id{};
  std::array<std::uint8_t, 32> hash{};
};
std::array<std::uint8_t, 32> device_hash(std::string_view token) noexcept;
// Which device the Cookie field's en_device cookies name: each that is a token is hashed and compared with every
// device's hash in full, so the time does not say which matched; none, or a field past kMaxCookie, is nullopt.
std::optional<std::size_t> match_device(std::optional<std::string_view> cookie_field,
                                        std::span<const DeviceKey> devices, std::string_view* token = nullptr) noexcept;
// With `token`, the matched cookie's value is set there too (to renew it), else left empty.
// A name the player gives a device: 1 to kMaxDeviceName characters of UTF-8, no control character, not only spaces.
bool valid_device_name(std::string_view name) noexcept;
// A device's id: kDeviceIdChars of lowercase hex.
bool valid_device_id(std::string_view id) noexcept;

// The six-digit code "Pair a device" shows in game: for kCodeLife, once, and void after kCodeTries wrong tries.
class PairingCode {
 public:
  using Clock = std::chrono::steady_clock;
  enum class Try : std::uint8_t {
    paired,  // the code was right: it is used, and void from now
    wrong,   // not the code; after kCodeTries of them the code is void
    none,    // no code is showing: never started, used, expired or void
  };
  // A new code from the random bytes (rejection sampling, 4 bytes a draw), replacing any shown; false if every draw
  // was rejected (a chance below 1 in 10^50 with 64 bytes), and then none is showing.
  bool start(std::span<const std::uint8_t> random, Clock::time_point now) noexcept;
  Try attempt(std::string_view code, Clock::time_point now) noexcept;
  bool showing(Clock::time_point now) const noexcept;
  // The code while it shows, else empty.
  std::string_view code(Clock::time_point now) const noexcept;
  Clock::time_point expires() const noexcept { return expires_; }
  void cancel() noexcept;

 private:
  std::array<char, kCodeDigits> code_{};
  bool live_ = false;
  Clock::time_point expires_{};
  std::size_t wrong_ = 0;
};

// The pairing page an unpaired device sees for any page load on the network listener, with its script and style:
// GraceUI's own, built in, in the Grace look, every text 4.5:1 (invariant 13); it posts {code, name} as JSON.
std::string_view pairing_page() noexcept;
std::string_view pairing_script() noexcept;
std::string_view pairing_style() noexcept;

}  // namespace grace::serve
