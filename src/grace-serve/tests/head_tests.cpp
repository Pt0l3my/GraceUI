// The request-head parser (head.h): what it takes, every refusal with its status, the order of its decisions, its caps
// at and past their limits, and no allocation while it parses.
#include <doctest/doctest.h>

#include <string>
#include <string_view>

#include "alloc_counter.h"
#include "grace/serve/head.h"
#include "support.h"

using namespace grace::serve;
using namespace grace::serve::test;

namespace {

// Feeds the whole of `bytes` and says why it was refused, or none.
Refusal refusal_of(std::string_view bytes, const HeadRules& rules = loopback_rules()) {
  HeadParser parser(rules);
  const HeadParser::Fed fed = parser.feed(bytes);
  if (fed.state != HeadParser::State::refused) {
    return Refusal::none;
  }
  return parser.refusal();
}

std::string fields_of(std::size_t count) {
  std::string fields;
  for (std::size_t i = 0; i < count; ++i) {
    fields += "X-Field-" + std::to_string(i) + ": a\r\n";
  }
  return fields;
}

}  // namespace

TEST_CASE("a page load's head is taken whole, and the body's first byte is left") {
  const std::string head =
      "GET /index.html HTTP/1.1\r\n"
      "Host: 127.0.0.1:8642\r\n"
      "Sec-Fetch-Mode: navigate\r\n"
      "Sec-Fetch-Dest:document\r\n"
      "Accept: text/html, */*;q=0.8\r\n"
      "X-Obs-Text: caf\xC3\xA9\r\n"
      "\r\n";
  HeadParser parser(loopback_rules());
  const HeadParser::Fed fed = parser.feed(head + "extra");
  REQUIRE(fed.state == HeadParser::State::complete);
  CHECK(fed.consumed == head.size());
  const RequestHead& request = parser.head();
  CHECK(request.method == Method::get);
  CHECK(request.target == "/index.html");
  CHECK(request.path == "/index.html");
  CHECK_FALSE(request.has_query);
  CHECK(request.host == HostMatch::loopback);
  CHECK(request.size == head.size());
  CHECK(request.fields.size() == 5);
  CHECK(request.sec_fetch_mode == std::string_view("navigate"));
  CHECK(request.sec_fetch_dest == std::string_view("document"));  // no whitespace after the colon is fine
  CHECK(request.first("ACCEPT") == std::string_view("text/html, */*;q=0.8"));
  CHECK(request.first("x-obs-text") == std::string_view("caf\xC3\xA9"));
  CHECK_FALSE(request.first("cookie"));

  // Once complete, it takes nothing more.
  const HeadParser::Fed again = parser.feed("GET / HTTP/1.1\r\n");
  CHECK(again.state == HeadParser::State::complete);
  CHECK(again.consumed == 0);
}

TEST_CASE("a POST's Content-Length is read, and its body is left to the caller") {
  const std::string head =
      "POST /api/ticket HTTP/1.1\r\nHost: 127.0.0.1:8642\r\nContent-Length: 2\r\n"
      "Content-Type: application/json\r\n\r\n";
  HeadParser parser(loopback_rules());
  const HeadParser::Fed fed = parser.feed(head + "{}");
  REQUIRE(fed.state == HeadParser::State::complete);
  CHECK(fed.consumed == head.size());
  CHECK(parser.head().method == Method::post);
  CHECK(parser.head().has_content_length);
  CHECK(parser.head().content_length == 2);
}

TEST_CASE("a query string is reported, never decoded, and the path is the bytes before it") {
  HeadParser parser(loopback_rules());
  REQUIRE(parser.feed(get_head("/app.js?v=2&x=%2F?y")).state == HeadParser::State::complete);
  CHECK(parser.head().path == "/app.js");
  CHECK(parser.head().has_query);
  CHECK(parser.head().query == "v=2&x=%2F?y");

  HeadParser empty_query(loopback_rules());
  REQUIRE(empty_query.feed(get_head("/?")).state == HeadParser::State::complete);
  CHECK(empty_query.head().path == "/");
  CHECK(empty_query.head().has_query);
  CHECK(empty_query.head().query.empty());

  HeadParser encoded(loopback_rules());
  REQUIRE(encoded.feed(get_head("/a%20b/%2e%2E/c")).state == HeadParser::State::complete);
  CHECK(encoded.head().path == "/a%20b/%2e%2E/c");  // matched to the manifest as it is (§7.2 item 21)
}

TEST_CASE("the request line: GET and POST, origin form and HTTP/1.1 only") {
  CHECK(refusal_of("PUT / HTTP/1.1\r\nHost: 127.0.0.1:8642\r\n\r\n") == Refusal::method_not_allowed);
  CHECK(refusal_of("get / HTTP/1.1\r\nHost: 127.0.0.1:8642\r\n\r\n") == Refusal::method_not_allowed);
  CHECK(refusal_of("HEAD / HTTP/1.1\r\nHost: 127.0.0.1:8642\r\n\r\n") == Refusal::method_not_allowed);
  CHECK(refusal_of("OPTIONS / HTTP/1.1\r\nHost: 127.0.0.1:8642\r\n\r\n") == Refusal::method_not_allowed);
  CHECK(status_of(Refusal::method_not_allowed) == 405);
  CHECK(refusal_of("G@T / HTTP/1.1\r\nHost: 127.0.0.1:8642\r\n\r\n") == Refusal::bad_request_line);

  CHECK(refusal_of("GET http://127.0.0.1:8642/ HTTP/1.1\r\nHost: 127.0.0.1:8642\r\n\r\n") == Refusal::bad_target);
  CHECK(refusal_of("GET * HTTP/1.1\r\nHost: 127.0.0.1:8642\r\n\r\n") == Refusal::bad_target);
  CHECK(refusal_of("CONNECT 127.0.0.1:8642 HTTP/1.1\r\nHost: 127.0.0.1:8642\r\n\r\n") == Refusal::bad_target);
  CHECK(refusal_of(get_head("/a#b")) == Refusal::bad_target);
  CHECK(refusal_of(get_head("/a\"b")) == Refusal::bad_target);
  CHECK(refusal_of(get_head("/a\\b")) == Refusal::bad_target);
  CHECK(refusal_of(get_head("/a%2")) == Refusal::bad_target);
  CHECK(refusal_of(get_head("/a%zz")) == Refusal::bad_target);
  CHECK(refusal_of(get_head("/caf\xC3\xA9")) == Refusal::bad_target);
  CHECK(refusal_of(get_head("/a\tb")) == Refusal::bad_target);
  CHECK(status_of(Refusal::bad_target) == 400);

  CHECK(refusal_of("GET / HTTP/1.0\r\nHost: 127.0.0.1:8642\r\n\r\n") == Refusal::wrong_version);
  CHECK(refusal_of("GET / HTTP/2.0\r\nHost: 127.0.0.1:8642\r\n\r\n") == Refusal::wrong_version);
  CHECK(status_of(Refusal::wrong_version) == 505);
  CHECK(refusal_of("GET / http/1.1\r\nHost: 127.0.0.1:8642\r\n\r\n") == Refusal::bad_request_line);
  CHECK(refusal_of("GET / HTTP/11\r\nHost: 127.0.0.1:8642\r\n\r\n") == Refusal::bad_request_line);
  CHECK(refusal_of("GET / HTTP/1.1 \r\nHost: 127.0.0.1:8642\r\n\r\n") == Refusal::bad_request_line);
  CHECK(refusal_of("GET  / HTTP/1.1\r\nHost: 127.0.0.1:8642\r\n\r\n") == Refusal::bad_request_line);
  CHECK(refusal_of(" GET / HTTP/1.1\r\nHost: 127.0.0.1:8642\r\n\r\n") == Refusal::bad_request_line);
  CHECK(refusal_of("GET /a b HTTP/1.1\r\nHost: 127.0.0.1:8642\r\n\r\n") == Refusal::bad_request_line);
  CHECK(refusal_of("GET\t/ HTTP/1.1\r\nHost: 127.0.0.1:8642\r\n\r\n") == Refusal::bad_request_line);
  CHECK(refusal_of("GET /\r\nHost: 127.0.0.1:8642\r\n\r\n") == Refusal::bad_request_line);
}

TEST_CASE("a target of kMaxTarget bytes is taken, and one more is 414") {
  const std::string longest = "/" + std::string(kMaxTarget - 1, 'a');
  HeadParser parser(loopback_rules());
  CHECK(parser.feed(get_head(longest)).state == HeadParser::State::complete);
  CHECK(refusal_of(get_head(longest + "a")) == Refusal::target_too_long);
  CHECK(status_of(Refusal::target_too_long) == 414);
  // A request line that runs past the head's cap is 414 too, decided at the cap.
  CHECK(refusal_of("GET /" + std::string(kMaxHead, 'a')) == Refusal::target_too_long);
  CHECK(refusal_of("GET /" + std::string(2000, 'a') + " HTTP/1.1\r\nHost: 127.0.0.1:8642\r\nX: " +
                   std::string(kMaxHead, 'b')) == Refusal::target_too_long);
  CHECK(refusal_of(std::string(kMaxHead + 1, 'G')) == Refusal::bad_request_line);
}

TEST_CASE("line ends: CRLF only, with no NUL, and no empty line first") {
  CHECK(refusal_of("GET / HTTP/1.1\nHost: 127.0.0.1:8642\n\n") == Refusal::bare_lf);
  CHECK(refusal_of("GET / HTTP/1.1\r\nHost: 127.0.0.1:8642\n\r\n") == Refusal::bare_lf);
  CHECK(refusal_of("GET / HTTP/1.1\r\nHost: 127.0.0.1:8642\r\r\n\r\n") == Refusal::bare_cr);
  CHECK(refusal_of("GET / HTTP/1.1\r\nX: a\rb\r\nHost: 127.0.0.1:8642\r\n\r\n") == Refusal::bare_cr);
  CHECK(refusal_of(std::string("GET / HTTP/1.1\r\nX: a\0b\r\nHost: 127.0.0.1:8642\r\n\r\n", 48)) ==
        Refusal::nul_byte);
  CHECK(refusal_of("\r\nGET / HTTP/1.1\r\nHost: 127.0.0.1:8642\r\n\r\n") == Refusal::leading_empty_line);
  CHECK(status_of(Refusal::bare_lf) == 400);
  CHECK(status_of(Refusal::bare_cr) == 400);
  CHECK(status_of(Refusal::nul_byte) == 400);

  // These are decided as they arrive, before the head is whole.
  HeadParser parser(loopback_rules());
  CHECK(parser.feed("GET / HTTP/1.1\r\nHost: x\n").state == HeadParser::State::refused);
  HeadParser waiting(loopback_rules());
  CHECK(waiting.feed("GET / HTTP/1.1\r").state == HeadParser::State::need_more);  // a CR waits for its LF
  CHECK(waiting.feed("x").state == HeadParser::State::refused);
  CHECK(waiting.refusal() == Refusal::bare_cr);
}

TEST_CASE("field lines: no folding, no space before the colon, a token for a name and no control in a value") {
  CHECK(refusal_of(get_head("/", "X-A: one\r\n two\r\n")) == Refusal::obs_fold);
  CHECK(refusal_of(get_head("/", "X-A: one\r\n\ttwo\r\n")) == Refusal::obs_fold);
  CHECK(refusal_of("GET / HTTP/1.1\r\n Host: 127.0.0.1:8642\r\n\r\n") == Refusal::obs_fold);
  CHECK(refusal_of(get_head("/", "X-A : one\r\n")) == Refusal::space_before_colon);
  CHECK(refusal_of(get_head("/", "X-A\t: one\r\n")) == Refusal::space_before_colon);
  CHECK(refusal_of("GET / HTTP/1.1\r\nHost : 127.0.0.1:8642\r\n\r\n") == Refusal::space_before_colon);
  CHECK(refusal_of(get_head("/", "X-A one\r\n")) == Refusal::bad_field_name);
  CHECK(refusal_of(get_head("/", ": one\r\n")) == Refusal::bad_field_name);
  CHECK(refusal_of(get_head("/", "X(A): one\r\n")) == Refusal::bad_field_name);
  CHECK(refusal_of(get_head("/", "X A: one\r\n")) == Refusal::bad_field_name);
  CHECK(refusal_of(get_head("/", "X-A: o\x01ne\r\n")) == Refusal::bad_field_value);
  CHECK(refusal_of(get_head("/", "X-A: one\x7F\r\n")) == Refusal::bad_field_value);
  CHECK(status_of(Refusal::obs_fold) == 400);
  CHECK(status_of(Refusal::space_before_colon) == 400);

  HeadParser parser(loopback_rules());
  REQUIRE(parser.feed(get_head("/", "X-Empty:\r\nX-Tabs:\t a\tb \t\r\n")).state == HeadParser::State::complete);
  CHECK(parser.head().first("x-empty") == std::string_view(""));
  CHECK(parser.head().first("x-tabs") == std::string_view("a\tb"));
}

TEST_CASE("Host: exactly one, and the check comes first after the syntax") {
  CHECK(refusal_of("GET / HTTP/1.1\r\n\r\n") == Refusal::missing_host);
  CHECK(refusal_of("GET / HTTP/1.1\r\nX-A: b\r\n\r\n") == Refusal::missing_host);
  CHECK(refusal_of(get_head("/", "Host: 127.0.0.1:8642\r\n")) == Refusal::duplicate_field);
  CHECK(refusal_of(get_head("/", "hOsT: 127.0.0.1:8642\r\n")) == Refusal::duplicate_field);
  CHECK(status_of(Refusal::missing_host) == 400);
  CHECK(status_of(Refusal::duplicate_field) == 400);

  const auto with_host = [](std::string_view host, const HeadRules& rules = loopback_rules()) {
    return refusal_of("GET / HTTP/1.1\r\nHost: " + std::string(host) + "\r\n\r\n", rules);
  };
  CHECK(with_host("127.0.0.1:8642") == Refusal::none);
  CHECK(with_host("localhost:8642") == Refusal::none);
  CHECK(with_host("[::1]:8642") == Refusal::none);
  CHECK(with_host("attacker.example:8642") == Refusal::wrong_host);
  CHECK(with_host("127.0.0.1") == Refusal::wrong_host);
  CHECK(with_host("127.0.0.1:8643") == Refusal::wrong_host);
  CHECK(with_host("127.0.0.1:08642") == Refusal::wrong_host);
  CHECK(with_host("LOCALHOST:8642") == Refusal::wrong_host);
  CHECK(with_host("") == Refusal::wrong_host);
  CHECK(with_host("192.0.2.7:8642") == Refusal::wrong_host);
  CHECK(with_host("192.0.2.7:8642", network_rules()) == Refusal::none);
  CHECK(with_host("[2001:db8::7]:8642", network_rules()) == Refusal::none);
  CHECK(with_host("127.0.0.1:8642", network_rules()) == Refusal::wrong_host);  // a loopback Host there is 421
  CHECK(with_host("localhost:8642", network_rules()) == Refusal::wrong_host);
  CHECK(status_of(Refusal::wrong_host) == 421);

  // The Host check runs once the head is whole and well formed, before the method and the framing are looked at.
  CHECK(refusal_of("PUT / HTTP/1.1\r\nHost: attacker.example\r\n\r\n") == Refusal::wrong_host);
  CHECK(refusal_of("GET / HTTP/1.1\r\nHost: attacker.example\r\nTransfer-Encoding: chunked\r\n\r\n") ==
        Refusal::wrong_host);
  CHECK(refusal_of("GET / HTTP/1.1\r\nHost: attacker.example\r\nX A: b\r\n\r\n") == Refusal::bad_field_name);
  CHECK(refusal_of("GET / HTTP/1.0\r\nHost: attacker.example\r\n\r\n") == Refusal::wrong_version);

  HeadParser parser(network_rules());
  REQUIRE(parser.feed("GET / HTTP/1.1\r\nHost: [2001:db8::7]:8642\r\n\r\n").state == HeadParser::State::complete);
  CHECK(parser.head().host == HostMatch::network);
  CHECK(parser.head().host_address == 1);
}

TEST_CASE("the body's framing: one Content-Length of plain digits, no Transfer-Encoding and no Expect") {
  const auto post = [](std::string_view fields) {
    return refusal_of("POST /api HTTP/1.1\r\nHost: 127.0.0.1:8642\r\n" + std::string(fields) + "\r\n");
  };
  CHECK(post("Content-Length: 0\r\n") == Refusal::none);
  CHECK(post("Content-Length: 007\r\n") == Refusal::none);
  CHECK(post("Content-Length:  12 \r\n") == Refusal::none);
  CHECK(post("") == Refusal::post_without_length);
  CHECK(post("Content-Length: +5\r\n") == Refusal::bad_content_length);
  CHECK(post("Content-Length: -5\r\n") == Refusal::bad_content_length);
  CHECK(post("Content-Length: 5 5\r\n") == Refusal::bad_content_length);
  CHECK(post("Content-Length: 5,5\r\n") == Refusal::bad_content_length);
  CHECK(post("Content-Length: 0x10\r\n") == Refusal::bad_content_length);
  CHECK(post("Content-Length:\r\n") == Refusal::bad_content_length);
  CHECK(post("Content-Length: 18446744073709551616\r\n") == Refusal::bad_content_length);
  CHECK(post("Content-Length: 18446744073709551615\r\n") == Refusal::body_too_large);
  CHECK(post("Content-Length: 5\r\nContent-Length: 5\r\n") == Refusal::duplicate_field);
  CHECK(post("Content-Length: " + std::to_string(kMaxUpload) + "\r\n") == Refusal::none);
  CHECK(post("Content-Length: " + std::to_string(kMaxUpload + 1) + "\r\n") == Refusal::body_too_large);
  CHECK(post("Transfer-Encoding: chunked\r\n") == Refusal::transfer_encoding);
  CHECK(post("Content-Length: 5\r\nTransfer-Encoding: chunked\r\n") == Refusal::transfer_encoding);
  CHECK(post("Transfer-Encoding: identity\r\n") == Refusal::transfer_encoding);
  CHECK(post("Content-Length: 5\r\nExpect: 100-continue\r\n") == Refusal::expectation);
  CHECK(status_of(Refusal::post_without_length) == 400);
  CHECK(status_of(Refusal::bad_content_length) == 400);
  CHECK(status_of(Refusal::body_too_large) == 413);
  CHECK(status_of(Refusal::transfer_encoding) == 501);
  CHECK(status_of(Refusal::expectation) == 417);

  CHECK(refusal_of(get_head("/", "Content-Length: 0\r\n")) == Refusal::none);
  CHECK(refusal_of(get_head("/", "Content-Length: 5\r\n")) == Refusal::get_with_body);
  CHECK(refusal_of(get_head("/", "Transfer-Encoding: chunked\r\n")) == Refusal::transfer_encoding);
  CHECK(status_of(Refusal::get_with_body) == 400);

  // A route's own cap, through the rules.
  HeadRules small = loopback_rules();
  small.max_content_length = 1024;
  CHECK(refusal_of("POST /api HTTP/1.1\r\nHost: 127.0.0.1:8642\r\nContent-Length: 1024\r\n\r\n", small) ==
        Refusal::none);
  CHECK(refusal_of("POST /api HTTP/1.1\r\nHost: 127.0.0.1:8642\r\nContent-Length: 1025\r\n\r\n", small) ==
        Refusal::body_too_large);
}

TEST_CASE("the fields a rule reads may appear once each") {
  for (const std::string_view name : {"Origin", "Cookie", "Content-Type", "Upgrade", "Sec-WebSocket-Key",
                                      "Sec-WebSocket-Version", "Sec-Fetch-Site", "Sec-Fetch-Mode", "Sec-Fetch-Dest"}) {
    CAPTURE(name);
    const std::string twice = std::string(name) + ": a\r\n" + std::string(name) + ": a\r\n";
    CHECK(refusal_of(get_head("/", twice)) == Refusal::duplicate_field);
  }
  // Other fields may repeat, and Connection's tokens are read across all of them.
  HeadParser parser(loopback_rules());
  REQUIRE(parser.feed(get_head("/", "Connection: keep-alive\r\nAccept: a\r\nAccept: b\r\nConnection: , Upgrade \r\n"))
              .state == HeadParser::State::complete);
  CHECK(parser.head().has_token("connection", "upgrade"));
  CHECK(parser.head().has_token("CONNECTION", "Keep-Alive"));
  CHECK_FALSE(parser.head().has_token("connection", "close"));
  CHECK_FALSE(parser.head().has_token("accept", "upgrade"));
}

TEST_CASE("the head's caps: its size, its fields and its Cookie") {
  // kMaxFields fields, Host among them, are taken; one more is 431.
  HeadParser parser(loopback_rules());
  CHECK(parser.feed(get_head("/", fields_of(kMaxFields - 1))).state == HeadParser::State::complete);
  CHECK(refusal_of(get_head("/", fields_of(kMaxFields))) == Refusal::too_many_fields);
  CHECK(status_of(Refusal::too_many_fields) == 431);

  // A head of exactly kMaxHead bytes is taken; one byte more is 431.
  const std::string base = get_head("/", "X-Pad: \r\n");
  const std::string pad(kMaxHead - base.size(), 'p');
  const std::string exact = get_head("/", "X-Pad: " + pad + "\r\n");
  REQUIRE(exact.size() == kMaxHead);
  HeadParser at_cap(loopback_rules());
  CHECK(at_cap.feed(exact).state == HeadParser::State::complete);
  CHECK(refusal_of(get_head("/", "X-Pad: " + pad + "p\r\n")) == Refusal::head_too_large);
  CHECK(status_of(Refusal::head_too_large) == 431);
  // The refusal comes at the cap, with nothing past it taken.
  HeadParser over(loopback_rules());
  const HeadParser::Fed fed = over.feed(get_head("/", "X-Pad: " + std::string(kMaxHead, 'p') + "\r\n"));
  CHECK(fed.state == HeadParser::State::refused);
  CHECK(fed.consumed == kMaxHead);

  // A Cookie of kMaxCookie bytes is taken; one more is 431, whose page says to clear the cookies.
  HeadParser cookie(loopback_rules());
  CHECK(cookie.feed(get_head("/", "Cookie: " + std::string(kMaxCookie, 'c') + "\r\n")).state ==
        HeadParser::State::complete);
  CHECK(refusal_of(get_head("/", "Cookie: " + std::string(kMaxCookie + 1, 'c') + "\r\n")) ==
        Refusal::cookie_too_large);
  CHECK(status_of(Refusal::cookie_too_large) == 431);
  CHECK(refusal_body(Refusal::cookie_too_large).find("cookies") != std::string_view::npos);
}

TEST_CASE("reset makes the parser new, and a refusal is final") {
  HeadParser parser(loopback_rules());
  CHECK(parser.feed("GET / HTTP/1.1\n").state == HeadParser::State::refused);
  const HeadParser::Fed after = parser.feed(get_head());
  CHECK(after.state == HeadParser::State::refused);
  CHECK(after.consumed == 0);
  parser.reset();
  CHECK(parser.state() == HeadParser::State::need_more);
  CHECK(parser.feed(get_head("/again")).state == HeadParser::State::complete);
  CHECK(parser.head().path == "/again");
}

TEST_CASE("every refusal has a status, a name and a body that echoes nothing") {
  for (int i = 1; i <= static_cast<int>(Refusal::upgrade_not_get); ++i) {
    const auto refusal = static_cast<Refusal>(i);
    CAPTURE(name_of(refusal));
    CHECK(status_of(refusal) >= 400);
    CHECK(name_of(refusal) != "unknown");
    CHECK_FALSE(refusal_body(refusal).empty());
  }
  CHECK(refusal_body(Refusal::wrong_host).find("Host") == std::string_view::npos);
}

TEST_CASE("the parser allocates nothing") {
  const std::string head = get_head("/a?b", "Cookie: en_session=x\r\nConnection: Upgrade\r\nUpgrade: websocket\r\n");
  const std::string refused_head = get_head("/", "X-Pad: " + std::string(kMaxHead, 'p') + "\r\n");
  const HeadRules rules = loopback_rules();
  grace::abi::test::Counting counting;
  HeadParser whole(rules);
  whole.feed(head);
  HeadParser bytewise(rules);
  for (const char c : head) {
    bytewise.feed(std::string_view(&c, 1));
  }
  HeadParser refused(rules);
  refused.feed(refused_head);
  const grace::abi::test::Counts counts = counting.counts();
  CHECK(whole.state() == HeadParser::State::complete);
  CHECK(bytewise.state() == HeadParser::State::complete);
  CHECK(refused.state() == HeadParser::State::refused);
  CHECK(counts.allocations == 0);
}
