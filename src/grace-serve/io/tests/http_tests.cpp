// http_tests.cpp: the server security suite's HTTP half (§9.1), over real sockets on loopback: Host (421), Origin and
// Fetch Metadata (403), the session cookie only on a top-level page load on the loopback listener, the ticket route,
// item 10's headers on every response and no CORS header, only the files graceui.toml lists (a query string 404), the
// redirects of item 23, and the notice page when a listed file fails its check (invariants 4 to 6).
#include <doctest/doctest.h>

#include <fstream>
#include <iterator>
#include <string>

#include "harness.h"

using namespace grace::serve;
using namespace grace::serve::iotest;
namespace protocol = grace::protocol;

namespace {

constexpr std::string_view kCsp =
    "default-src 'self'; script-src 'self'; style-src 'self'; img-src 'self' data:; font-src 'self'; "
    "connect-src 'self'; frame-ancestors 'none'; base-uri 'none'; form-action 'none'";

// Item 10 on every response, error pages included, and no CORS header (item 9).
void check_security_headers(const Reply& reply) {
  CAPTURE(reply.head);
  CHECK(reply.field("content-security-policy") == std::string(kCsp));
  CHECK(reply.field("x-content-type-options") == "nosniff");
  CHECK(reply.field("referrer-policy") == "no-referrer");
  CHECK(reply.field("cross-origin-opener-policy") == "same-origin");
  CHECK(reply.field("cross-origin-resource-policy") == "same-origin");
  for (const auto& [name, value] : reply.fields) {
    CHECK_FALSE(name.starts_with("access-control-"));
  }
  if (reply.status != 101) {
    CHECK(reply.field("content-length") == std::to_string(reply.body.size()));
    CHECK(reply.field("connection").has_value());
    CHECK(reply.closed);  // Connection: close, and the server closed it (§6.8)
  }
}

}  // namespace

TEST_CASE("a page load gets the app and the session cookie, HttpOnly, SameSite=Strict, Path=/, host-only") {
  Fixture fixture;
  const Reply reply = request(fixture.port, page_load(fixture.port));
  REQUIRE(reply.status == 200);
  CHECK(reply.body == fixture.folder->index);
  CHECK(reply.field("content-type") == "text/html; charset=utf-8");
  CHECK(reply.field("cache-control") == "no-store");
  CHECK(reply.field("connection") == "close");
  const std::optional<std::string> set = reply.field("set-cookie");
  REQUIRE(set.has_value());
  const std::string cookie = cookie_from(reply);
  CHECK(cookie.size() == 43);
  CHECK(*set == "en_session=" + cookie + "; Path=/; HttpOnly; SameSite=Strict");  // no Domain, no expiry (item 3)
  check_security_headers(reply);
  // The same cookie for the session's every page load; a script's fetch gets none.
  CHECK(cookie_from(request(fixture.port, page_load(fixture.port, "/index.html"))) == cookie);
  const Reply script = request(fixture.port, plain_get(fixture.port, "/assets/app.js",
                                                       "Sec-Fetch-Site: same-origin\r\nSec-Fetch-Mode: no-cors\r\n"
                                                       "Sec-Fetch-Dest: script\r\n"));
  CHECK(script.status == 200);
  CHECK(script.body == fixture.folder->script);
  CHECK(script.field("content-type") == "text/javascript; charset=utf-8");
  CHECK_FALSE(script.field("cache-control").has_value());  // a hashed script is cached; the HTML is not (§6.4)
  CHECK_FALSE(script.field("set-cookie").has_value());
  check_security_headers(script);
}

TEST_CASE("the cookie only on a top-level page load, on the loopback listener, with the Host 127.0.0.1") {
  Fixture fixture;
  const std::uint16_t port = fixture.port;
  // No Fetch Metadata at all (an older browser, or the address typed into a tool): a page load.
  CHECK(cookie_from(request(port, plain_get(port, "/"))).size() == 43);
  // A frame's navigation is not top-level; nor is a fetch.
  CHECK(cookie_from(request(port, plain_get(port, "/", "Sec-Fetch-Site: cross-site\r\nSec-Fetch-Mode: navigate\r\n"
                                                       "Sec-Fetch-Dest: iframe\r\n")))
            .empty());
  CHECK(cookie_from(request(port, plain_get(port, "/", "Sec-Fetch-Site: same-origin\r\nSec-Fetch-Mode: cors\r\n"
                                                       "Sec-Fetch-Dest: empty\r\n")))
            .empty());
  // On localhost the page load is redirected first, with no cookie (D112).
  const Reply localhost = request(port, page_load(port, "/", "", "localhost:" + std::to_string(port)));
  CHECK(localhost.status == 307);
  CHECK(cookie_from(localhost).empty());
  // A 404 page load on 127.0.0.1 still gets it: any top-level page load does (item 3).
  CHECK(cookie_from(request(port, page_load(port, "/nothing-here"))).size() == 43);
}

TEST_CASE("Host is checked before anything else: 421 for any other, with no echo; 400 when missing or repeated") {
  Fixture fixture;
  const std::uint16_t port = fixture.port;
  for (const std::string& host : {std::string("evil.example:") + std::to_string(port), std::string("127.0.0.1:1"),
                                 std::string("127.0.0.1"), "LOCALHOST:" + std::to_string(port),
                                 "127.0.0.1:0" + std::to_string(port)}) {
    CAPTURE(host);
    for (const std::string_view path : {"/", "/api/ws", "/api/ticket", "/no-such-file"}) {
      const Reply reply = request(port, plain_get(port, path, "", host));
      CHECK(reply.status == 421);
      CHECK(reply.body.find(host) == std::string::npos);
      CHECK(reply.head.find(host) == std::string::npos);
      check_security_headers(reply);
    }
  }
  CHECK(request(port, "GET / HTTP/1.1\r\n\r\n").status == 400);
  const Reply twice = request(port, "GET / HTTP/1.1\r\nHost: " + host_of(port) + "\r\nHost: " + host_of(port) + "\r\n\r\n");
  CHECK(twice.status == 400);
  check_security_headers(twice);
}

TEST_CASE("every response carries item 10's headers, a Content-Length and Connection: close, and no CORS header") {
  Fixture fixture;
  const std::uint16_t port = fixture.port;
  const std::string cookie = fixture.cookie();
  struct Case {
    std::string request;
    int status;
  };
  const std::vector<Case> cases = {
      {page_load(port), 200},
      {plain_get(port, "/assets/app.js"), 200},
      {plain_get(port, "/stray.html"), 404},
      {plain_get(port, "/", "", "evil.example"), 421},
      {"GET / HTTP/1.1\nHost: " + host_of(port) + "\n\n", 400},
      {"PUT / HTTP/1.1\r\nHost: " + host_of(port) + "\r\n\r\n", 405},
      {"GET / HTTP/1.1\r\nHost: " + host_of(port) + "\r\nTransfer-Encoding: chunked\r\n\r\n", 501},
      {"GET / HTTP/1.1\r\nHost: " + host_of(port) + "\r\nExpect: 100-continue\r\n\r\n", 417},
      {"GET / HTTP/1.0\r\nHost: " + host_of(port) + "\r\n\r\n", 505},
      {"GET /" + std::string(1100, 'a') + " HTTP/1.1\r\nHost: " + host_of(port) + "\r\n\r\n", 414},
      {plain_get(port, "/", "Cookie: " + std::string(5000, 'a') + "\r\n"), 431},
      {ticket_post(port, cookie, "", "{}", false), 403},
      {ticket_post(port, cookie, "", std::string(300, ' ')), 413},
      {upgrade_head(port, cookie, "", false), 403},
      {page_load(port, "/", "", "localhost:" + std::to_string(port)), 307},
  };
  for (const Case& one : cases) {
    CAPTURE(one.request.substr(0, 80));
    const Reply reply = request(port, one.request);
    CHECK(reply.status == one.status);
    check_security_headers(reply);
  }
  WsClient ws;
  REQUIRE(ws.open(port, cookie) == 101);
  check_security_headers(parse_reply(ws.response()));
}

TEST_CASE("only the files graceui.toml lists, matched byte for byte; a query string is 404") {
  Fixture fixture;
  const std::uint16_t port = fixture.port;
  CHECK(request(port, plain_get(port, "/index.html")).status == 200);
  CHECK(request(port, plain_get(port, "/assets/app.js")).status == 200);
  for (const std::string_view path :
       {"/stray.html", "/graceui.toml", "/web/index.html", "/assets/", "/assets", "/index.html/", "//index.html",
        "/./index.html", "/assets/../index.html", "/%69ndex.html", "/INDEX.HTML", "/index.html?v=1", "/?", "/api",
        "/api/", "/api/ws/x", "/api/ticket?x", "/api/ws?ticket=abc"}) {
    CAPTURE(path);
    const Reply reply = request(port, plain_get(port, path));
    CHECK(reply.status == 404);
    CHECK(reply.body.find(fixture.folder->index) == std::string::npos);
    check_security_headers(reply);
  }
  // A path out of origin form never reaches the files (item 19).
  CHECK(request(port, plain_get(port, "../graceui.toml")).status == 400);
  CHECK(request(port, "GET http://127.0.0.1/ HTTP/1.1\r\nHost: " + host_of(port) + "\r\n\r\n").status == 400);
  // POST to a file: static files by GET only (item 9).
  const Reply post = request(port, "POST /index.html HTTP/1.1\r\nHost: " + host_of(port) +
                                       "\r\nContent-Length: 0\r\n\r\n");
  CHECK(post.status == 404);
}

TEST_CASE("a localhost or [::1] page load is redirected to 127.0.0.1, its Location from the listed name alone") {
  Fixture fixture;
  const std::uint16_t port = fixture.port;
  const std::string origin = origin_of(port);
  for (const std::string& host : {"localhost:" + std::to_string(port), "[::1]:" + std::to_string(port)}) {
    CAPTURE(host);
    const auto location = [&](std::string_view path) {
      const Reply reply = request(port, page_load(port, path, "", host));
      CHECK(reply.status == 307);
      CHECK(reply.field("cache-control") == "no-store");
      check_security_headers(reply);
      return reply.field("location").value_or("");
    };
    CHECK(location("/") == origin + "/");
    CHECK(location("/index.html") == origin + "/index.html");
    CHECK(location("/assets/app.js") == origin + "/assets/app.js");
    // Nothing of the request reaches the header (item 19): an unlisted path, a query, an encoding all become "/".
    CHECK(location("/stray.html") == origin + "/");
    CHECK(location("/index.html?next=//evil.example") == origin + "/");
    CHECK(location("/%2F%2Fevil.example") == origin + "/");
    // A byte origin-form does not allow is refused before routing: 400, and no Location at all.
    const Reply bad = request(port, page_load(port, "/x\x7f", "", host));
    CHECK(bad.status == 400);
    CHECK_FALSE(bad.field("location").has_value());
  }
  // A subresource on localhost is served as it is (item 5 allows the Host), and no cookie goes with it.
  const Reply script =
      request(port, plain_get(port, "/assets/app.js", "Sec-Fetch-Mode: no-cors\r\nSec-Fetch-Dest: script\r\n",
                              "localhost:" + std::to_string(port)));
  CHECK(script.status == 200);
  CHECK_FALSE(script.field("set-cookie").has_value());
}

TEST_CASE("[::1] is bound too where the system has it, and serves as 127.0.0.1 does") {
  Fixture fixture;
  if (!fixture.server.stats().ipv6) {
    MESSAGE("this system has no ::1: skipped");
    return;
  }
  const std::uint16_t port = fixture.port;
  const Reply reply = request(port, page_load(port, "/", "", "[::1]:" + std::to_string(port)), 3s, true);
  CHECK(reply.status == 307);
  CHECK(reply.field("location") == origin_of(port) + "/");
  const Reply direct = request(port, page_load(port), 3s, true);
  CHECK(direct.status == 200);
  CHECK(cookie_from(direct).size() == 43);  // the peer is ::1 and the Host 127.0.0.1:PORT (item 3)
}

TEST_CASE("the ticket: POST with Origin, Fetch Metadata, the cookie and a JSON body within its cap") {
  Fixture fixture;
  const std::uint16_t port = fixture.port;
  const std::string cookie = fixture.cookie();
  const Reply good = request(port, ticket_post(port, cookie));
  REQUIRE(good.status == 200);
  CHECK(good.field("content-type") == "application/json");
  CHECK(good.field("cache-control") == "no-store");
  CHECK_FALSE(good.field("set-cookie").has_value());
  const protocol::Json body = protocol::Json::parse(good.body);
  CHECK(body["ticket"].get<std::string>().size() == 43);
  CHECK(body["ticket"].get<std::string>() != cookie);
  check_security_headers(good);

  const auto status = [&](const std::string& raw) { return request(port, raw).status; };
  CHECK(status(ticket_post(port, cookie, "", "{}", false)) == 403);                              // no Origin
  CHECK(status(ticket_post(port, cookie, "Origin: http://evil.example\r\n", "{}", false)) == 403);
  CHECK(status(ticket_post(port, cookie, "Origin: http://localhost:" + std::to_string(port) + "\r\n", "{}", false)) ==
        403);  // one origin (item 23)
  CHECK(status(ticket_post(port, cookie, "Origin: null\r\n", "{}", false)) == 403);
  CHECK(status(ticket_post(port, "")) == 403);                                                   // no cookie
  CHECK(status(ticket_post(port, std::string(43, 'A'))) == 403);                                 // a stale one
  const std::string cross =
      "POST /api/ticket HTTP/1.1\r\nHost: " + host_of(port) + "\r\nOrigin: " + origin_of(port) +
      "\r\nSec-Fetch-Site: cross-site\r\nContent-Type: application/json\r\nCookie: en_session=" + cookie +
      "\r\nContent-Length: 2\r\n\r\n{}";
  CHECK(status(cross) == 403);
  // A cookie set by another program on 127.0.0.1, with a narrower Path, is sent first; the real one still counts.
  CHECK(status(ticket_post(port, "", "Cookie: en_session=" + std::string(43, 'B') + "; en_session=" + cookie +
                                         "; other=1\r\n")) == 200);
  CHECK(status(ticket_post(port, "", "Cookie: " + std::string(5000, 'x') + "\r\n")) == 431);  // a flood
  const std::string text_body =
      "POST /api/ticket HTTP/1.1\r\nHost: " + host_of(port) + "\r\nOrigin: " + origin_of(port) +
      "\r\nContent-Type: text/plain\r\nCookie: en_session=" + cookie + "\r\nContent-Length: 2\r\n\r\n{}";
  CHECK(status(text_body) == 415);
  CHECK(status(ticket_post(port, cookie, "", std::string(257, ' '))) == 413);
  CHECK(status(plain_get(port, "/api/ticket", "Cookie: en_session=" + cookie + "\r\n")) == 404);
  // The ticket's body may arrive after its head, within the head's deadline.
  Client slow;
  REQUIRE(slow.connect(port));
  const std::string raw = ticket_post(port, cookie, "", "{\"a\":1}");
  REQUIRE(slow.send(raw.substr(0, raw.size() - 4)));
  std::this_thread::sleep_for(50ms);
  REQUIRE(slow.send(raw.substr(raw.size() - 4)));
  std::string answer;
  CHECK(slow.read_to_close(answer, 2s));
  CHECK(parse_reply(answer).status == 200);
}

TEST_CASE("a body shorter than its Content-Length gets no answer, and is closed at the head's deadline") {
  Fixture fixture;
  const std::uint16_t port = fixture.port;
  Client client;
  REQUIRE(client.connect(port));
  REQUIRE(client.send(ticket_post(port, fixture.cookie(), "", "{\"short\":1}").substr(0, 200)));
  const auto start = std::chrono::steady_clock::now();
  std::string bytes;
  CHECK(client.read_to_close(bytes, 3s));
  CHECK(bytes.empty());
  CHECK(std::chrono::steady_clock::now() - start < 1500ms);
}

TEST_CASE("a listed file that fails its check: the notice page for page loads, 404 for the rest") {
  Fixture::Options options;
  options.damaged = true;
  Fixture fixture(options);
  const std::uint16_t port = fixture.port;
  REQUIRE(fixture.wait_files());
  CHECK_FALSE(fixture.server.stats().files_ok);
  const Reply page = request(port, page_load(port));
  CHECK(page.status == 503);
  CHECK(page.field("content-type") == "text/html; charset=utf-8");
  CHECK(page.field("cache-control") == "no-store");
  CHECK(page.body.find("The browser view is unavailable") != std::string::npos);
  CHECK(page.body.find(fixture.folder->path.string()) == std::string::npos);
  check_security_headers(page);
  CHECK(request(port, plain_get(port, "/assets/app.js", "Sec-Fetch-Mode: no-cors\r\nSec-Fetch-Dest: script\r\n"))
            .status == 404);
  // One log line names the file by its relative name, never a path (PV5).
  bool named = false;
  for (const std::string& line : fixture.logs()) {
    CHECK(line.find(fixture.folder->path.string()) == std::string::npos);
    named = named || line.find("web/index.html") != std::string::npos;
  }
  CHECK(named);
}

TEST_CASE("a missing web folder or graceui.toml: the notice page; files from memory: served without a list") {
  {
    Fixture::Options options;
    options.no_folder = true;
    Fixture fixture(options);
    REQUIRE(fixture.wait_files());
    CHECK(request(fixture.port, page_load(fixture.port)).status == 503);
    CHECK(request(fixture.port, plain_get(fixture.port, "/assets/app.js", "Sec-Fetch-Mode: no-cors\r\n")).status ==
          404);
  }
  {
    Fixture::Options options;
    options.from_memory = true;
    Fixture fixture(options);
    REQUIRE(fixture.wait_files());
    CHECK(fixture.server.stats().files_ok);
    const Reply page = request(fixture.port, page_load(fixture.port));
    CHECK(page.status == 200);
    CHECK(page.body == fixture.folder->index);
    CHECK(request(fixture.port, plain_get(fixture.port, "/stray.html")).status == 404);
  }
}

TEST_CASE("graceui.toml as GraceUI's release tool writes it (D165): its web files, and nothing else, listed") {
  std::ifstream file(std::string(GRACE_FIXTURES) + "/release/graceui.toml", std::ios::binary);
  REQUIRE(file.good());
  const std::string text((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
  std::vector<ListedFile> files;
  std::string why;
  REQUIRE_MESSAGE(parse_manifest(text, files, why), why);
  REQUIRE(files.size() == 2);
  CHECK(files[0].name == "assets/app-1a2b3c.js");
  CHECK(files[0].size == 4096);
  CHECK(files[0].sha256[0] == 0x44);
  CHECK(files[1].name == "index.html");
}

TEST_CASE("graceui.toml's subset: what it takes and what fails the list") {
  const std::string hash(64, 'a');
  const auto entry = [&](std::string_view name, std::string_view size = "10") {
    return "\"" + std::string(name) + "\" = { size = " + std::string(size) + ", sha256 = \"" + hash + "\" }\n";
  };
  std::vector<ListedFile> files;
  std::string why;
  const std::string good = "# a comment\r\ngraceui = \"0.2.0\"  # after a value\nabi = [1, 0]\ncaps = [\"a\", \"b\",]\n"
                           "flag = true\n\n[files]\n" +
                           entry("graceui.dll") + entry("web/index.html") + entry("web/assets/app.3f9c.js") +
                           entry("fonts/face.ttf");
  REQUIRE_MESSAGE(parse_manifest(good, files, why), why);
  REQUIRE(files.size() == 2);
  CHECK(files[0].name == "index.html");
  CHECK(files[0].size == 10);
  CHECK(files[0].sha256[0] == 0xAA);
  CHECK(files[1].name == "assets/app.3f9c.js");
  const std::vector<std::pair<std::string, std::string>> bad = {
      {"twice", "[files]\n" + entry("web/a.html") + entry("web/a.html")},
      {"a table twice", "[files]\n[files]\n"},
      {"an array of tables", "[[files]]\n"},
      {"an escape", "a = \"x\\u0041\"\n"},
      {"a dotted key", "a.b = 1\n"},
      {"a float", "a = 1.5\n"},
      {"a leading zero", "a = 01\n"},
      {"a sign", "a = -1\n"},
      {"no size", "[files]\n\"web/a.html\" = { sha256 = \"" + hash + "\" }\n"},
      {"more keys", "[files]\n\"web/a.html\" = { size = 1, sha256 = \"" + hash + "\", x = 1 }\n"},
      {"an upper-case hash", "[files]\n\"web/a.html\" = { size = 1, sha256 = \"" + std::string(64, 'A') + "\" }\n"},
      {"a short hash", "[files]\n\"web/a.html\" = { size = 1, sha256 = \"abc\" }\n"},
      {"a parent segment", "[files]\n" + entry("web/../graceui.toml")},
      {"a hidden name", "[files]\n" + entry("web/.env.json")},
      {"an empty segment", "[files]\n" + entry("web/assets//a.js")},
      {"a space", "[files]\n" + entry("web/a b.html")},
      {"a backslash", "[files]\n\"web/a\\\\b.html\" = { size = 1, sha256 = \"" + hash + "\" }\n"},
      {"the server's routes", "[files]\n" + entry("web/api/ticket.json")},
      {"an unknown type", "[files]\n" + entry("web/run.exe")},
      {"no type", "[files]\n" + entry("web/README")},
      {"too large", "[files]\n" + entry("web/a.js", "16777217")},
      {"a bare CR", "a = 1\rb = 2\n"},
      {"trailing text", "a = 1 b\n"},
      {"not UTF-8", "a = \"\xff\"\n"},
  };
  for (const auto& [name, text] : bad) {
    CAPTURE(name);
    CHECK_FALSE(parse_manifest(text, files, why));
    CHECK(files.empty());
    CHECK(why.starts_with("graceui.toml"));
  }
  std::string many = "[files]\n";
  for (int i = 0; i < 5; ++i) {
    many += entry("web/f" + std::to_string(i) + ".js", "16000000");
  }
  CHECK_FALSE(parse_manifest(many, files, why));  // past 64 MiB in all
  CHECK(content_type_of("a.woff2") == "font/woff2");
  CHECK(content_type_of("a.HTML").empty());
  CHECK(content_type_of(".html").empty());
  CHECK(valid_web_name("assets/app~1.js"));
  CHECK_FALSE(valid_web_name("api"));
  CHECK_FALSE(valid_web_name(std::string(256, 'a')));
}
