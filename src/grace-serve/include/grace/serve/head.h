// head.h: the HTTP/1.1 request-head parser (RFC 9112, the narrow subset of G30 and §7.2 items 19 and 20). It is fed the
// bytes of one connection as they arrive, into its own fixed buffer of kMaxHead bytes, and answers need more, complete or
// refused with the status to answer. Pure: no sockets, no clocks, no allocation.
//
// The order of its decisions, the same however the bytes are split:
//   1. As the bytes arrive: a NUL, a bare CR, a bare LF or an empty line before the request line is 400 at once (a head
//      holding one can never become valid), and a head that has not ended within kMaxHead bytes is 414 when its request
//      line is what is too long, otherwise 431.
//   2. Once the whole head is in, its syntax: the request line (400, then 505 for another version, then 414, then 400 for
//      a target not in origin form), then each field line in order (400; 431 past kMaxFields fields or a Cookie past
//      kMaxCookie), then a missing Host (400).
//   3. Then the Host check (421, item 5), before anything else is looked at (item 20).
//   4. Then the method (405), Transfer-Encoding (501), the body's length (400, 413) and Expect (417).
// The path is the caller's to route, byte for byte (item 21); a query string is reported, for a 404 on a static path.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

#include "grace/serve/limits.h"
#include "grace/serve/refusal.h"

namespace grace::serve {

enum class Method : std::uint8_t { get, post };

// Which allowed name a request's Host matched (§7.2 items 5 and 23).
enum class HostMatch : std::uint8_t {
  none,           // 421
  loopback,       // 127.0.0.1:PORT, the dashboard's one origin
  localhost,      // localhost:PORT: a page load is redirected to 127.0.0.1 (item 23)
  ipv6_loopback,  // [::1]:PORT: likewise
  network,        // one of the network listener's own addresses with the port (item 16)
};

// The listener a connection arrived on.
struct Listener {
  bool network = false;      // false: the loopback listener
  std::uint16_t port = 8642;  // D58's default
  // The network listener's own addresses as a Host names them, without the port ("192.0.2.7", "[2001:db8::7]"); the
  // loopback listener's names are fixed and this is ignored. The caller keeps them alive while the parser lives.
  std::span<const std::string_view> addresses;
};

struct HeadRules {
  Listener listener;
  // The largest Content-Length taken; the route's own cap is the caller's, below it (§7.2 items 9 and 18).
  std::uint64_t max_content_length = kMaxUpload;
};

struct Field {
  std::string_view name;   // as sent
  std::string_view value;  // without the whitespace around it
};

// A complete head's view: every string_view points into the parser's own buffer, valid while it lives unreset.
struct RequestHead {
  Method method = Method::get;
  std::string_view target;  // the request-target, as sent
  std::string_view path;    // the target up to its first '?'
  std::string_view query;   // after that '?'
  bool has_query = false;
  HostMatch host = HostMatch::none;
  std::size_t host_address = 0;  // with HostMatch::network: which of the listener's addresses
  bool has_content_length = false;
  std::uint64_t content_length = 0;
  std::size_t size = 0;  // the head's bytes, its final empty line included: the body starts here
  std::span<const Field> fields;

  // The fields a rule reads that may appear at most once (a second is 400), found without case.
  std::optional<std::string_view> host_field;
  std::optional<std::string_view> origin;
  std::optional<std::string_view> cookie;
  std::optional<std::string_view> content_type;
  std::optional<std::string_view> upgrade;
  std::optional<std::string_view> sec_websocket_key;
  std::optional<std::string_view> sec_websocket_version;
  std::optional<std::string_view> sec_fetch_site;
  std::optional<std::string_view> sec_fetch_mode;
  std::optional<std::string_view> sec_fetch_dest;

  // The first field of this name, compared without case.
  std::optional<std::string_view> first(std::string_view name) const noexcept;
  // Whether a comma-separated field (Connection) holds this token, across every field of that name, without case.
  bool has_token(std::string_view name, std::string_view token) const noexcept;
};

class HeadParser {
 public:
  enum class State : std::uint8_t { need_more, complete, refused };
  struct Fed {
    State state;
    std::size_t consumed;  // the bytes of this call the head took; once complete, the rest are the body's
  };

  explicit HeadParser(const HeadRules& rules) noexcept;
  HeadParser(const HeadParser&) = delete;
  HeadParser& operator=(const HeadParser&) = delete;

  // Takes bytes until the head is complete or refused; once it is, takes no more.
  Fed feed(std::string_view bytes) noexcept;

  State state() const noexcept { return state_; }
  // Valid once complete.
  const RequestHead& head() const noexcept { return head_; }
  // Once refused: why, and status_of() gives the status to answer.
  Refusal refusal() const noexcept { return refusal_; }

  // Ready for another head, as new.
  void reset() noexcept;

 private:
  void refuse(Refusal refusal) noexcept;
  void finish() noexcept;
  Refusal at_cap() const noexcept;
  Refusal parse_field(std::string_view line) noexcept;
  Refusal admit() noexcept;

  HeadRules rules_;
  State state_ = State::need_more;
  Refusal refusal_ = Refusal::none;
  std::size_t size_ = 0;
  std::size_t line_start_ = 0;
  std::size_t first_line_end_ = 0;  // the request line's CR, once it has ended (it is never empty: that is refused)
  std::string_view method_;
  bool transfer_encoding_ = false;
  bool expect_ = false;
  std::size_t field_count_ = 0;
  RequestHead head_{};
  std::array<Field, kMaxFields> fields_{};
  std::array<char, kMaxHead> buffer_{};
};

}  // namespace grace::serve
