# The server's C++ basis: an independent review (SRV)

An independent review the owner asked for on 2026-11-16, for G30 (the C++ basis of GraceUI's loopback server, DESIGN
§7.2), kept as it was received apart from this header. "You" and "your" in it mean GraceUI. Its sources were read on
that day; each fact is marked **[V]** (verified from the source given) or **[R]** (remembered, not checked). Its
quotations are short and sourced. Nobody was contacted. The owner decided G30 the same day: our own server, built to
section 5's plan.

## 1. Recommendation

**Write your own, on the narrow subset you describe.** Runner-up: **Boost.Beast**. I would not take civetweb,
cpp-httplib or any of the frameworks.

- **Main reason.** All ten of your rules are your code whichever basis you pick; a library removes only the request
  parser and the WebSocket framing, under a thousand lines by my estimate (5.4). In exchange it brings features you must turn off and keep
  turned off, and those features are where the 2025-26 bugs were: chunked bodies, form and multipart parsing, URI
  decoding, HTTP/2, compression, file serving (section 3.1). Your subset has none of them.
- **Main risk.** A memory-safety bug of your own in the game's process, reachable before authentication, and from any
  device on the network once the opt-in mode is on. It is controlled by design (section 5), not by review alone: the
  parsers as pure functions over a bounded buffer, every cap checked before any allocation, fuzzed in CI.
- **When to take Beast instead.** If you cannot commit to the fuzzing and conformance plan in section 5.4. Beast is
  only the protocol layer ("not a client or server"), so your rules and your threads stay yours, and it is the one
  candidate with two outside audits and continuous fuzzing. Its cost is Boost and Asio at build time.
- **http.sys:** your belief is right in substance; do not use it (section 4).
- **One correction to the brief:** "at most two connections at once" can hold for WebSockets, not for HTTP. Browsers
  open several connections to one host and Chrome opens idle ones that send nothing (section 5.2).

## 2. Candidates

| Candidate | Licence, AI clause | WebSocket server | Windows, MSVC ABI | Threads and stop | Size, dependencies | Maintenance | Security record |
|---|---|---|---|---|---|---|---|
| **Own server** | yours | what you write | yes | exactly yours | about 2,000-3,000 lines (my estimate) | yours | yours; fuzz it |
| **Boost.Beast** | BSL-1.0, standard text, no AI clause [V] | yes; protocol layer only [V] | yes [R] | your threads run the `io_context`; `stop()` [R] | header-only; needs Boost, not stand-alone Asio [V] | Boost 1.92.0 current, with Beast parser fixes [V] | two outside audits; fuzzers in OSS-Fuzz [V] |
| **civetweb** | MIT, licence file read, no AI clause [V] | yes [V] | yes [V] | its own pool of a set size; `mg_stop` [R] | one large C file [R] | last release v1.16, 2023-04-10; fixes on master only [V] | heap overflow and DoS CVEs in 2025 [V] |
| **cpp-httplib** | MIT, standard text, no AI clause [V] | yes [V] | yes [V] | blocking; pool grows to 4x; one thread plus a heartbeat thread per WebSocket [V] | one header [V] | v0.66.0; five releases 7-22 Oct 2026 [V] | many security fixes in 2026 [V]; OSS-Fuzz [V] |
| **uWebSockets** | Apache-2.0 "where such explicit notice is given"; forks must rename [V] | yes [V] | needs uSockets, and libuv on Windows [R] | one app per thread [V] | uSockets (+ libuv) | v20.81.0, 26 Oct [V] | OSS-Fuzz [V] |
| **libwebsockets** | MIT core [V]; licence file not read | yes | yes [V] | its own loop, serviced on your thread [R] | large | v5.0 [V] | critical HTTP/2 CVE, Sept 2026 [V] |
| **IXWebSocket** | BSD-3-Clause [V] | yes; "some tests still failing in the server code" [V] | tested [V] | callbacks on its background thread [V] | medium | its README says the main developer lacks time [V] | no fuzzing mentioned [V] |
| **Crow** | BSD-3-Clause [R] | yes [R] | yes [R] | Asio's [R] | needs Asio [R] | v1.3.4, 7 Sep 2026 [V] | five advisories in that release [V] |
| **Drogon** | MIT [R] | yes [R] | yes [R] | its own loops [R] | a framework with its own libraries [R] | v1.9.14, 12 Oct [V] | none labelled [V] |
| **websocketpp** | BSD-3-Clause [R] | yes | yes [R] | Asio's [R] | needs Asio [R] | 0.8.2; 407 open issues, 77 open pull requests [V] | unmaintained, by those numbers |
| **llhttp** (parser only; my addition) | MIT per its README [V]; licence file not read (rate limited) | no | C, yes [R] | none: a parser | generated C | v9.4.3, 30 Jul [V] | strict by default [V] |

Mongoose (GPLv2 or commercial) and libmicrohttpd (LGPL) stay excluded [R]: linking GPL code statically makes the
whole DLL GPL, and LGPL code linked statically obliges you to ship what a user needs to relink.

## 3. Notes on the candidates

### 3.1 What the 2025-26 record shows

Every general-purpose candidate I checked shipped parser or framing fixes in the last eighteen months:

- **civetweb.** CVE-2025-55763, a heap overflow in the URI parser, versions 1.14 to 1.16; CVE-2025-9648, an endless
  loop in `mg_handle_form_request` on a null byte in POST form data, 1.10 to 1.16, CVSS 8.7. Both are fixed by commits
  on master; the releases page still shows v1.16 (10 April 2023) as the latest, so no tagged release carries them.
  You would be tracking master. [V] https://security-tracker.debian.org/tracker/CVE-2025-55763 ,
  https://osv.dev/vulnerability/CVE-2025-9648 , https://github.com/civetweb/civetweb/releases ,
  https://sourceforge.net/projects/civetweb/files (the year of v1.16).
- **cpp-httplib.** Releases 0.60.0 to 0.63.0 between 7 and 22 October 2026 each carry security fixes: stricter
  request-line parsing, more than one `Host` field rejected, a WebSocket upgrade with a body no longer parsed as the
  next request, canonical paths only on mount points. v0.57.0 fixed chunk-size parsing that allowed request
  smuggling, and made `pre_request_handler` run for WebSocket routes: before it, authentication in that handler was
  skipped on the upgrade path. CVE-2026-28434: an exception's text sent in a response header, before 0.35.0. The
  project is fixing fast, which is good, but its behaviour is moving under you. [V]
  https://github.com/yhirose/cpp-httplib/releases ,
  https://newreleases.io/project/github/yhirose/cpp-httplib/release/v0.57.0 ,
  https://osv.dev/vulnerability/CVE-2026-28434 . Further 2026 CVE ids appeared in search results (CVE-2026-21428,
  -22776, -34441, -45372); I did not open them.
- **Crow.** v1.3.4 (7 September 2026) fixes five advisories, two of them exactly your concerns: a global
  middleware's rejection ignored during the WebSocket upgrade (authentication bypass), and fragmented WebSocket
  messages passing the maximum payload limit. CVE-2026-38967: response header injection up to v1.3.1. [V]
  https://github.com/CrowCpp/Crow/releases , https://osv.dev/vulnerability/CVE-2026-38967
- **libwebsockets.** CVE-2026-19773, CVSS 9.8: an out-of-bounds write in the HTTP/2 HPACK parser, published
  2026-09-15. Six CVEs are listed for 2025-26, one more of them critical (CVE-2025-1866; details not opened). [V]
  https://osv.dev/vulnerability/CVE-2026-19773 , https://cvefeed.io/vuln/product/166619/warmcatlibwebsockets/
- **Boost.Beast.** Even the best of them: Boost 1.92.0 makes `websocket::stream` reject 64-bit frame lengths with the
  high bit set, and makes the HTTP parser reject `Content-Length` with `Transfer-Encoding` whatever the header order.
  [V] https://www.boost.org/users/download/ , https://www.boost.org/releases/1.92.0.beta1

Two readings. First, most of these are in features your subset does not have, which is the case for writing your
own. Second, the last item is a warning to you: the rules people get wrong are the ones in section 5.3, and mature
libraries were still getting them wrong in 2026.

### 3.2 Boost.Beast (runner-up)

- "This library is not a client or server"; "Beast only works with Boost, not stand-alone Asio"; "Sources are
  header-only". [V, from the 1.84 documentation; I did not open the 1.92 page]
  https://www.boost.org/doc/libs/1_84_0/libs/beast/doc/html/beast/introduction.html
- Licence: the repository's `LICENSE_1_0.txt` is the standard Boost Software License 1.0, nothing added. [V]
  https://github.com/boostorg/beast/blob/develop/LICENSE_1_0.txt
- Audits. An earlier assessment found three denial-of-service bugs in the code that decompresses WebSocket messages
  (fixed before Beast's first Boost release) and one weakness in client-side masking. [V; the page gives no date]
  https://www.bishopfox.com/case_study/securing-beast/ . The 2023-24 audit of Boost 1.83.0 had Beast in scope and one
  Beast finding, of medium severity: CRLF injection in HTTP headers, left open because the maintainers hold that
  sanitising header values is the caller's job. The auditors wrote 15 fuzzers, merged in OSS-Fuzz. [V]
  https://www.shielder.com/blog/2024/05/boost-security-audit
- Consequences for you: never put request data into a response header (you would not anyway); do not enable
  permessage-deflate; set `read_message_max`. Boost 1.92.0 is the current release; the download page says Boost is
  released in April, August and December, so I take 1.92.0 as August 2026 (inferred, no date on the page).
- "Large for a mod" is a build-time cost: header-only, nothing extra ships, and the DLL grows by what you
  instantiate. I have no measured figure. [R] Asio on Windows may start a helper thread of its own for timers [R]:
  check that against your "two threads" rule before choosing it.

### 3.3 The others, briefly

- **civetweb** fits your shape least badly of the servers (C, one file, a callback before its routing [R]), but see
  3.1: an unreleased master is what you would ship. Its licence file also covers Lua, SQLite, Duktape and zlib, each
  only if built in. [V] https://github.com/civetweb/civetweb/blob/master/LICENSE.md . The repository page shows 211
  open issues and 37 open pull requests. [V] https://github.com/civetweb/civetweb . An `oss-fuzz` project folder
  named `civetweb` exists in a mirror of OSS-Fuzz (undated snapshot). [V]
  https://android.googlesource.com/platform/external/oss-fuzz/+/02cee920/projects/
- **cpp-httplib** does serve WebSockets now (your question): `Server::WebSocket()`, RFC 6455 without extensions. But
  it is "a blocking I/O WebSocket implementation using a thread-per-connection model (plus one heartbeat thread per
  connection)", on a pool that starts at 8 threads or more and grows to four times that. That is not "a fixed small
  number of threads". Its README also documents `SO_REUSEADDR` as the Windows default, which you would have to
  replace (section 7). [V] https://llvm.googlesource.com/third_party/cpp-httplib/+/HEAD/README-websocket.md ,
  https://github.com/yhirose/cpp-httplib , https://github.com/yhirose/cpp-httplib/blob/master/LICENSE
- **uWebSockets** is well fuzzed (its README: OSS-Fuzz, about 95% coverage, a perfect Autobahn score) and its
  "one app per thread" model would fit. Against it: two more dependencies on Windows, a design aimed at very many
  connections, and a README that says "all rights reserved" with Apache-2.0 applying "where such explicit notice is
  given" and requires modified forks to take another name. Read its licence file yourselves; I read the README
  only. [V] https://github.com/uNetworking/uWebSockets , https://www.GitHub.com/uNetworking/uWebSockets/releases
- **libwebsockets**: far more than you need (HTTP/2, many event loops, TLS back ends), and 3.1. [V]
  https://github.com/warmcat/libwebsockets
- **IXWebSocket**: its own README says the server code has failing tests and that the main developer lacks time for
  the project. [V] https://github.com/machinezone/IXWebSocket
- **Drogon** is a whole framework; **websocketpp** has had no release since 0.8.2. [V]
  https://github.com/drogonframework/drogon/releases , https://github.com/zaphoyd/websocketpp
- **llhttp**, a middle road: keep your own sockets, rules and WebSocket framing, and take only the request parser
  from Node.js's parser (generated C, strict unless you set a lenient flag). It parses all of HTTP/1.1, so you would
  still refuse chunked bodies and count header bytes yourself in its callbacks [R]. For GET and POST with
  `Content-Length` only, I think a strict parser of your own is small enough to fuzz to saturation, and I would
  keep llhttp as the fallback. [V] https://github.com/nodejs/llhttp , https://github.com/nodejs/llhttp/releases

## 4. http.sys

Confirmed in substance:

- "By default the namespace is closed": only an administrator can register a URL prefix until a reservation is made,
  and making a reservation needs administrator rights. [V]
  https://learn.microsoft.com/pl-pl/windows/win32/http/namespace-reservations-registrations-and-routing ,
  https://learn.microsoft.com/en-us/archive/blogs/drnick/configuring-http-for-windows-vista
- One exception, reported and matching what I remember: `http://localhost:PORT/` above port 1024 registers without
  elevation. So loopback by the name `localhost` would work; `127.0.0.1` and every network address would need
  `netsh http add urlacl` run by an administrator. [V, a 2023 mirror of a Stack Overflow answer; not official]
  https://kiwix.lema.org/stackoverflow.com_en_all_2023-11/questions/6721153/httpaddurl-on-localhost-fails-for-non-admin-users
- Beyond that: requests are parsed and routed by prefix in the kernel driver before your code sees them, so "Host
  check before any routing" cannot be yours; the port is shared with any other process's prefixes; WebSocket needs
  the separate Windows 8 component. [R]

## 5. If you write your own

### 5.1 Shape

- **Protocol code as pure functions.** `parse_head(bytes) -> need more | request | error` and
  `decode_frame(bytes) -> need more | frame | error`, with no sockets, clocks or allocation inside. This is what makes
  them fuzzable, and portable to a Linux CI job.
- **One non-blocking I/O thread** over all sockets (`WSAPoll` or an IOCP; with fewer than 20 sockets either will
  do), and one worker for disk reads and the upload's SHA-256. A blocking thread per connection cannot work in two
  threads (5.2).
- **`Connection: close` on every HTTP response.** No keep-alive and no pipelining means no state between requests;
  a new connection per asset costs nothing on loopback.
- **Exact-match static files.** Compare the raw request path, byte for byte, with the manifest's keys. No
  percent-decoding, no normalising, no joining of paths: the traversal class of bug then cannot exist. Decide what a
  query string does (I would answer 404 if one is present on a static path).
- **Fixed per-connection buffers** of the cap's size (say 8 KiB for a head, 16 KiB plus header for a frame from a
  page). A length that exceeds the cap closes the connection before anything is allocated.

### 5.2 HTTP: what is usually got wrong

- **Idle and speculative connections.** Chrome opens extra connections ahead of need that send nothing; a
  server that waits on one stalls the real request. [V]
  https://hackernoon.com/chrome-preconnect-breaks-singly-threaded-servers-95944be16400 . Browsers also load assets on
  several parallel connections per host (six in Chrome [R]). So: cap WebSockets at two, give HTTP its own cap (8 to
  16), and time out a connection that has not sent a complete head.
- **Deadlines are absolute, not per byte.** A head must be complete within, say, 5 s of accept, whatever trickles
  in; a body within a time set by its declared length; a write must drain or the connection goes. A per-read timeout
  is what Slowloris defeats.
- **Framing.** Refuse any `Transfer-Encoding` (501, close). Refuse more than one `Content-Length`, or one that is not
  plain digits (no sign, no spaces inside, overflow checked). RFC 9112: differing or invalid lengths are "an
  unrecoverable error", and a server "MUST respond with a 400 (Bad Request) status code and then close the
  connection". [V] https://www.rfc-editor.org/rfc/rfc9112
- **Header syntax.** Whitespace between a field name and the colon: the server "MUST reject, with a response status
  code of 400". Obsolete line folding: reject. A bare CR: invalid. A lone LF "MAY" be accepted; refuse it, you serve
  browsers. A request with no `Host`, or more than one: 400. [V] same RFC. Compare header names without case; cap the
  number of fields and the total head.
- **Request line.** Method exactly `GET` or `POST` (405 otherwise, with `Allow`); origin-form target only (starts
  with `/`; refuse `http://...` and `*`); `HTTP/1.1` only; an over-long target gets 414.
- **Rule order.** Parse the head fully, then the Host check, before any look at the path, exactly as your rule 2
  says. The 421's body must not echo the `Host` value.
- **Upgrade.** `Connection` is a comma-separated token list (Firefox sends `keep-alive, Upgrade` [R]); `Upgrade:
  websocket` without case; `Sec-WebSocket-Version: 13`, otherwise 426 with the version header [V, RFC 6455 4.2.2 and
  4.4]; `Sec-WebSocket-Key` must decode to 16 bytes; an upgrade request with a body is refused (see cpp-httplib,
  3.1). Ignore `Sec-WebSocket-Extensions` entirely: never negotiate compression.
- **Responses.** Always `Content-Length`; your header set on every path, including 400, 404, 421 and 426; no value
  taken from the request ever written into a header.
- **`Expect: 100-continue`:** browsers do not send it [R]; answer 417 and close.

### 5.3 WebSocket: what is usually got wrong

From RFC 6455 [V] https://www.rfc-editor.org/rfc/rfc6455 :

- "The server MUST close the connection upon receiving a frame that is not masked", and "A server MUST NOT mask any
  frames that it sends".
- Lengths: "the minimal number of bytes MUST be used to encode the length", and in the 64-bit form "the most
  significant bit MUST be 0". Check the length against your cap before reading the payload. (Beast began rejecting
  the high bit in 2026; Crow's 2026 advisory was fragmented messages passing the payload limit.)
- Reserved bits "MUST be 0 unless an extension is negotiated": with no extensions, any set bit fails the connection.
- "All control frames MUST have a payload length of 125 bytes or less and MUST NOT be fragmented." They may arrive
  between the fragments of a message, so the decoder needs both states at once.
- Fragments: a continuation with no message open, or a new data frame while one is open, fails the connection. Cap
  the reassembled message, not the frame.
- Text: "the whole message MUST contain valid UTF-8". Validate incrementally across fragments with a small state
  machine; reject overlong forms and surrogates.
- Close: a body, if present, starts with a 2-byte code, so a 1-byte body is an error; validate the code and the
  reason's UTF-8; echo a Close once, then shut the socket down with a bounded wait. Answer Ping with a Pong carrying
  the same payload.
- Yours, not the RFC's: the ticket's 2 s runs from the 101, as an absolute deadline; a bounded send queue per
  connection (a tab that does not read 256 KiB frames is closed, not buffered for); the command rate limit counted
  before the JSON is parsed.

### 5.4 A minimal fuzzing and test plan

- **Harnesses** (libFuzzer, with AddressSanitizer and UBSan): the head parser; the frame decoder; cookie, `Host` and
  `Origin` parsing; and one harness that drives the whole connection state machine from a byte stream.
- **Chunking differential:** feed each input whole, then byte by byte, then split at fuzzer-chosen points; the
  results must be identical. This finds most incremental-parser bugs.
- **Properties asserted in every harness:** no read past the buffer, no allocation above the caps, no state from
  which the connection neither progresses nor closes.
- **On Windows:** "libFuzzer now supports Windows", but "without ASAN is unsupported", and `/MD` and `/INCREMENTAL`
  builds are unsupported; it is in maintenance ("important bugs will get fixed"). [V]
  https://llvm.googlesource.com/llvm-project/llvm/+/refs/heads/main/docs/LibFuzzer.md . MSVC's own
  `/fsanitize=fuzzer` is "experimental". [V] https://learn.microsoft.com/en-us/cpp/build/reference/fsanitize . Because
  the parsers are pure, run the fuzzers on Linux in CI as well and keep the corpus in the repository: a few minutes
  on each pull request, longer nightly.
- **Conformance:** the Autobahn test suite's fuzzing client against your server for RFC 6455 [R]
  https://github.com/crossbario/autobahn-testsuite (not opened today). Unit tests from the RFCs' own examples.
- **Behaviour tests over real sockets:** a head sent one byte a second, a connection that sends nothing, a body
  shorter than its `Content-Length`, a tab that never reads, 100 connections at once.
- **Size** (my estimate, not a measurement): head parser 300-400 lines, response writer 150, handshake 150 (SHA-1 and
  Base64 from BCrypt and a small routine), frame codec with UTF-8 300-450, the I/O loop with connection state and
  deadlines 500-800; with the rules themselves, 2,000-3,000 lines, and about as much again in tests and harnesses.

## 6. Does the home-network mode change the answer?

It does not change the choice; it raises the bar for it, and it has limits no server fixes.

- **Everything before authentication is now reachable by raw bytes**: the head parser, the cookie parser, the pairing
  route and the WebSocket handshake. Fuzz those first. A library would not make this safer by itself: civetweb's 2025
  heap overflow was in exactly that position.
- **Separate listeners, separate budgets.** The network listener gets its own connection cap and smaller limits, so a
  device on the network cannot use up loopback's connections. Accept only peers with a private or link-local address;
  bind each address by name, never the wildcard; follow address changes.
- **The pairing code is six digits**, so one million guesses. It needs a small attempt budget, a new code after
  failures, a short life, and to be valid only while the player has the pairing screen open in game.
- **Plain HTTP on the network cannot be made private.** Anyone on the same network who can observe or redirect
  traffic can read the session cookie and every frame, and can alter the page in transit; your CSP does not help
  against that. So treat a network session as less trusted than a loopback one: nothing that touches files, keys or
  settings a remote reader should not see. You already keep upload to loopback; I would extend the idea.
- **A page on `http://192.168.x.x` is not a secure context.** MDN lists `127.0.0.0/8`, `::1/128` and `localhost` as
  potentially trustworthy; a private network address over plain HTTP meets none of its rules (my inference from the
  list). Features limited to secure contexts are missing there (service workers [V]; `crypto.subtle` [R]), so the web
  app must not depend on them. [V] https://developer.mozilla.org/en-US/docs/Web/Security/Secure_contexts
- **Build hardening** matters more in this mode: Control Flow Guard, stack cookies, and the standard library's
  hardened or checked mode in the server's translation units. [R]

## 7. A server inside the game's process

- **Start and stop outside `DllMain`.** Microsoft's list of what never to do there includes "Call CreateThread ...
  it is risky", "Synchronize with other threads. This can cause a deadlock", and `LoadLibrary`. [V]
  https://learn.microsoft.com/en-ie/Windows/Win32/dlls/dynamic-link-library-best-practices . `WSAStartup` likewise
  belongs outside it. [R]
- **Process exit.** "By the time DllMain is called at process exit, all the process's threads have been forcibly
  cleaned up ... the ideal DLL_PROCESS_DETACH handler is empty." [V] same page. So the server's threads can die at any
  instruction: keep nothing that needs flushing, and clear upload leftovers at the next start, not at exit.
- **Never unload with threads alive.** "Turned off by the player" should mean sockets closed and threads joined from
  an ordinary call, with the DLL staying loaded; pin the module if anything else could unload it. [R]
- **Stopping the I/O thread:** wake it with an event or a completion packet. Closing a socket from another thread to
  break a blocking call races with handle reuse. [R]
- **Sockets.** Set `SO_EXCLUSIVEADDRUSE` before `bind`: it "prevents other sockets from being forcibly bound to the
  same address and port", where `SO_REUSEADDR` lets a second socket overtake the port. Close by `shutdown` then drain:
  an exclusive socket "cannot always be reused immediately" while an accepted connection lingers. [V]
  https://learn.microsoft.com/en-ie/Windows/Win32/winsock/so-exclusiveaddruse . Create sockets non-inheritable, so no
  child process of the game holds your port. [R]
- **"Port taken" has a second cause.** Windows reserves port ranges for Hyper-V and WSL, and `bind` then fails with
  access denied although nothing listens (`netsh int ipv4 show excludedportrange protocol=tcp`). Your fail-closed
  message should name both causes. [R]
- **IPv6 loopback.** `::1` is its own socket (IPv6 only), and may be unavailable. `127.0.0.1`, `localhost` and `[::1]`
  are three origins with three cookie jars, and browsers may try `::1` first for `localhost` [R]: if another program
  holds `[::1]:PORT`, "localhost" reaches that program, not you. Show `http://127.0.0.1:PORT` in game, and consider
  answering the other two hosts with a redirect to it.
- **Firewall.** A loopback listener raises no Windows Firewall prompt; the first listen on a network address raises
  the alert for the game's executable. Reports agree with what I remember; I found no Microsoft page saying it. [V for
  the reports]
  https://kiwix.lema.org/stackoverflow.com_en_all_2023-11/questions/3375435/avoid-windows-firewall-popup-with-sockets-on-localhost
  . Allowing it needs an administrator; dismissing it leaves a block rule, after which the mode fails silently for
  other devices; the rule covers the executable, not your port. Explain this in game before the mode is turned on.
  [R]
- **Anti-virus and anti-cheat.** A game process with inline hooks that opens a listening socket looks like a
  backdoor to heuristics. What helps: loopback by default, no packing or obfuscation, reproducible builds with
  published hashes. Elden Ring mods run with its anti-cheat off [R]; nothing here should change that.
- **Threads in someone else's process.** Catch every exception at the top of each server thread (an escaped one ends
  the game); never touch game state from them, queue to the game's thread and time the answer out; use a monotonic
  clock for deadlines; name the threads for crash dumps.

## 8. What I could not verify

- Release years where GitHub showed day and month only: uWebSockets v20.81.0 (26 Oct), llhttp v9.4.3 (30 Jul), Drogon
  v1.9.14 (12 Oct), Crow v1.3.5. cpp-httplib's are 2026 (v0.63.0 was "25 days ago" today); civetweb's v1.16 is 2023.
- Licence files not opened: llhttp (rate limited twice), uWebSockets, libwebsockets, IXWebSocket, Crow, Drogon,
  websocketpp. For those the table gives the repository's stated licence, or my memory where marked. Read each file
  at the commit you would pin, as you already do.
- Commit dates on any default branch (GitHub's commit pages refused the fetch), and the current OSS-Fuzz project
  list (the same); the mirror snapshot I read is undated. It lists `boost`, `civetweb`, `cpp-httplib`, `uwebsockets`
  and `nodejs`, and does not list libwebsockets, Crow, Drogon, IXWebSocket or websocketpp.
- Binary size of any candidate, and Asio's helper thread on Windows: measure before choosing Beast.
- The firewall behaviour and the `localhost` exception in http.sys rest on reports and memory, not on Microsoft's
  own pages: test both on a clean Windows 11 account without administrator rights.
