// upload.cpp: the upload route (G28, D226, §7.2 item 18). A page asks for a one-use ticket over its WebSocket, bound to
// its session, the space, a file command of the space's and its args, and the file's size and SHA-256; then it sends the
// bytes in POST /upload on the loopback listener alone, with every check of an API call and the ticket. The bytes are
// read into a buffer of exactly the ticket's size, hashed on the worker (§7.6), and handed to the space with the
// command, whose result comes back to the session like any command's. GraceUI keeps nothing: the buffer is zeroed when
// the last holder lets go, and nothing touches the disk. No file name from the browser is ever read.
#include <algorithm>
#include <cstring>
#include <string>

#include "../ascii.h"
#include "engine.h"
#include "grace/serve/checks.h"
#include "grace/serve/codec.h"
#include "grace/serve/response.h"

namespace grace::serve {

FileBytes::~FileBytes() {
  // Zeroed before the memory goes back, so that no copy of the file lingers in the heap (§7.2 item 18).
  volatile std::uint8_t* bytes = bytes_.data();
  for (std::size_t i = 0; i < bytes_.size(); ++i) {
    bytes[i] = 0;
  }
}

namespace detail {
namespace {

constexpr std::string_view kPlainText = "text/plain; charset=utf-8";
constexpr std::string_view kReceived = "Received: the space has the file.\n";
constexpr std::string_view kBadTicket = "That upload's ticket is not valid: add the file again.\n";
constexpr std::string_view kWrongSize = "The file is not the size its ticket names: add it again.\n";
constexpr std::string_view kTooLarge = "The file is larger than 32 MiB.\n";
constexpr std::string_view kNotOctets = "Send the file's bytes as application/octet-stream.\n";
constexpr std::string_view kNoLength = "Send the file with its Content-Length.\n";
constexpr std::string_view kChanged = "The file changed on its way: add it again.\n";
constexpr std::string_view kSessionGone = "The page that asked for it was closed: add the file again.\n";
constexpr std::string_view kBusySpace = "The space has too many commands waiting: add the file again.\n";
constexpr std::string_view kNoMemory = "There is no room for the file just now: add it again.\n";

bool sha256_hex(std::string_view text, std::array<std::uint8_t, 32>& out) noexcept {
  constexpr std::string_view kHex = "0123456789abcdef";  // lowercase only, as the page writes it
  if (text.size() != 64) {
    return false;
  }
  for (std::size_t i = 0; i < 32; ++i) {
    const std::size_t high = kHex.find(text[2 * i]);
    const std::size_t low = kHex.find(text[2 * i + 1]);
    if (high == std::string_view::npos || low == std::string_view::npos) {
      return false;
    }
    out[i] = static_cast<std::uint8_t>(high << 4 | low);
  }
  return true;
}

}  // namespace

Connection* Engine::connection_of(std::uint64_t serial) noexcept {
  for (const auto& slot : pool_) {
    if (slot->serial == serial && slot->phase != Connection::Phase::idle) {
      return slot.get();
    }
  }
  return nullptr;
}

void Engine::upload_request(Connection& c, protocol::Upload& request, Clock::time_point now) {
  const std::shared_ptr<PortState> port = space_of(c, request.space);
  if (port == nullptr) {
    send_error(c, nullptr, protocol::ErrorCode::unknown_command,
               "upload " + std::to_string(request.id) + ": not a command of a space this view may show");
    return;
  }
  // From this PC alone: a paired device never sends a file (§7.2 item 18), before anything else is said.
  if (c.network) {
    send_result_error(c, *port, request.id, protocol::ErrorCode::not_now, "Only on the PC the game runs on.");
    return;
  }
  if (!port->has_command(request.name) || !file_command(port->description, request.name)) {
    send_result_error(c, *port, request.id, protocol::ErrorCode::unknown_command, "Not a command that takes a file.");
    return;
  }
  if (!confirmed(port->description, request.name, request.args)) {
    send_result_error(c, *port, request.id, protocol::ErrorCode::confirm_required);  // §7.3, D173
    return;
  }
  if (request.size == 0) {
    send_result_error(c, *port, request.id, protocol::ErrorCode::invalid_value, "The file is empty.");
    return;
  }
  if (request.size > protocol::kUploadMax) {
    send_result_error(c, *port, request.id, protocol::ErrorCode::invalid_value, "The file is larger than 32 MiB.");
    return;
  }
  std::array<std::uint8_t, 32> sha256{};
  if (!sha256_hex(request.sha256, sha256)) {
    send_result_error(c, *port, request.id, protocol::ErrorCode::bad_message, "A file's SHA-256 is 64 lowercase hex.");
    return;
  }
  // One upload at a time per session: a ticket not yet used, or a file on its way, holds the place.
  for (const UploadTicket& ticket : uploads_) {
    if ((ticket.live || ticket.busy) && ticket.session == c.serial) {
      send_result_error(c, *port, request.id, protocol::ErrorCode::busy,
                        "One file at a time: wait for the one on its way.");
      return;
    }
  }
  UploadTicket* slot = nullptr;
  for (UploadTicket& ticket : uploads_) {
    if (!ticket.live && !ticket.busy) {
      slot = &ticket;
      break;
    }
  }
  std::array<char, kTokenChars> token{};
  if (slot == nullptr || !new_token(token)) {
    send_result_error(c, *port, request.id, protocol::ErrorCode::busy, "Not now: another file is on its way.");
    return;
  }
  slot->live = true;
  slot->busy = false;
  slot->token = token;
  slot->session = c.serial;
  slot->port = port;
  slot->command = request.name;
  slot->args = std::move(request.args);
  slot->id = request.id;
  slot->size = request.size;
  slot->sha256 = sha256;
  slot->issued = now;
  protocol::UploadTicket answer;
  answer.space = port->description.info.id;
  answer.id = request.id;
  answer.ticket = std::string(token.data(), token.size());
  token.fill(0);
  send_message(c, protocol::Message{std::move(answer)});
}

void Engine::upload_post(Connection& c, std::string_view after, Clock::time_point now) {
  const RequestHead& head = c.parser.head();
  // Items 4, 6 and 7 as every API call: Origin, Fetch Metadata and the session cookie; then the ticket.
  const Refusal refusal =
      check_api_request(head, listener_, kSessionCookie, std::string_view(cookie_.data(), cookie_.size()));
  if (refusal != Refusal::none) {
    refuse(c, refusal, now);
    return;
  }
  if (!content_type_is_octets(head)) {
    respond_status(c, 415, kNotOctets, false, now);
    return;
  }
  if (!head.has_content_length) {
    respond_status(c, 411, kNoLength, false, now);
    return;
  }
  if (head.content_length > protocol::kUploadMax) {
    respond_status(c, 413, kTooLarge, false, now);  // at once, before a byte is read (§7.2 item 18)
    return;
  }
  std::size_t fields = 0;
  for (const Field& field : head.fields) {
    fields += iequals(field.name, kUploadTicketField) ? 1 : 0;
  }
  const std::optional<std::string_view> presented = head.first(kUploadTicketField);
  std::size_t found = uploads_.size();
  if (fields == 1 && presented && valid_token(*presented)) {
    for (std::size_t i = 0; i < uploads_.size(); ++i) {
      const UploadTicket& ticket = uploads_[i];
      // Every live ticket compared in full, as the WebSocket's tickets are.
      if (constant_time_equal(std::string_view(ticket.token.data(), ticket.token.size()), *presented) && ticket.live &&
          found == uploads_.size()) {
        found = i;
      }
    }
  }
  if (found == uploads_.size() || now - uploads_[found].issued >= config_.timings.upload_ticket) {
    respond_status(c, 403, kBadTicket, false, now);
    return;
  }
  UploadTicket& ticket = uploads_[found];
  // Used once, whatever follows: a wrong size, a dropped connection or a changed file ends it (§7.2 item 18).
  ticket.live = false;
  ticket.busy = true;
  ticket.token.fill(0);
  if (head.content_length != ticket.size) {
    respond_status(c, head.content_length > ticket.size ? 413 : 400, kWrongSize, false, now);
    upload_failed(found, protocol::ErrorCode::invalid_value, "The file was not the size its ticket named.", now);
    return;
  }
  try {
    c.upload = std::make_shared<FileBytes>(static_cast<std::size_t>(ticket.size));
  } catch (...) {
    respond_status(c, 503, kNoMemory, false, now);
    upload_failed(found, protocol::ErrorCode::busy, "There was no room for the file: add it again.", now);
    return;
  }
  c.upload_slot = found;
  c.upload_got = 0;
  c.phase = Connection::Phase::upload;
  c.deadline = now + config_.timings.upload;
  upload_bytes(c, after, now);
}

void Engine::upload_bytes(Connection& c, std::string_view data, Clock::time_point now) {
  const std::span<std::uint8_t> bytes = c.upload->bytes();
  const std::size_t take = std::min(data.size(), bytes.size() - c.upload_got);
  std::memcpy(bytes.data() + c.upload_got, data.data(), take);
  c.upload_got += take;
  if (c.upload_got < bytes.size()) {
    return;
  }
  // Whole: the worker checks its SHA-256 (§7.6), and the I/O thread waits for nothing.
  c.phase = Connection::Phase::hashing;
  c.deadline = now + config_.timings.upload;
  {
    std::lock_guard lock(worker_mutex_);
    hash_jobs_.push_back(HashJob{c.serial, c.upload});
  }
  worker_wake_.notify_one();
}

void Engine::take_hashes(Clock::time_point now) {
  std::deque<HashDone> done;
  {
    std::lock_guard lock(worker_mutex_);
    done.swap(hash_done_);
  }
  for (const HashDone& result : done) {
    Connection* c = connection_of(result.serial);
    if (c == nullptr || c->phase != Connection::Phase::hashing || c->upload == nullptr) {
      continue;  // the POST went away meanwhile: its ticket was failed then, and its file is let go with it
    }
    const std::size_t slot = c->upload_slot;
    UploadTicket& ticket = uploads_[slot];
    if (result.sha256 != ticket.sha256) {
      respond_status(*c, 400, kChanged, false, now);
      c->upload.reset();
      upload_failed(slot, protocol::ErrorCode::invalid_value, "The file changed on its way: add it again.", now);
      continue;
    }
    const Connection* session = connection_of(ticket.session);
    if (session == nullptr || session->phase != Connection::Phase::open || !ticket.port->registered.load()) {
      respond_status(*c, 409, kSessionGone, false, now);
      c->upload.reset();
      ticket = UploadTicket{};
      continue;
    }
    bool full = false;
    {
      std::lock_guard lock(ticket.port->mutex);
      if (ticket.port->commands.size() >= protocol::kCommandQueue) {
        full = true;
      } else {
        SpaceEvent event;
        event.kind = SpaceEvent::Kind::command;
        event.command = ticket.command;
        event.args = std::move(ticket.args);
        event.view = protocol::CommandView::browser;  // the loopback listener's alone
        event.reply = CommandRef{ticket.session, ticket.id, false};
        event.file = std::move(c->upload);
        ticket.port->commands.push_back(std::move(event));
      }
    }
    if (full) {
      respond_status(*c, 503, kBusySpace, false, now);
      c->upload.reset();
      upload_failed(slot, protocol::ErrorCode::rate_limited, "The space has too many commands waiting.", now);
      continue;
    }
    const std::shared_ptr<PortState> port = ticket.port;
    ticket = UploadTicket{};  // the place is free; the result comes to the session by its command's id
    Response response;
    response.status = 202;
    response.content_type = kPlainText;
    response.content_length = kReceived.size();
    respond(*c, write_response_head(response, c->head), kReceived, nullptr, now);
    log(LogLevel::info, "An upload went to its space (" + port->description.info.id + ").");
    if (port->wake) {
      try {
        port->wake();
      } catch (...) {
      }
    }
  }
}

void Engine::expire_uploads(Clock::time_point now) {
  for (std::size_t i = 0; i < uploads_.size(); ++i) {
    if (uploads_[i].live && now - uploads_[i].issued >= config_.timings.upload_ticket) {
      upload_failed(i, protocol::ErrorCode::not_now, "The file's ticket ran out: add it again.", now);
    }
  }
}

void Engine::upload_failed(std::size_t slot, protocol::ErrorCode code, std::string_view words,
                           Clock::time_point now) {
  UploadTicket& ticket = uploads_[slot];
  try {
    Connection* session = connection_of(ticket.session);
    if (session != nullptr && session->phase == Connection::Phase::open && ticket.port != nullptr) {
      send_result_error(*session, *ticket.port, ticket.id, code, std::string(words));
    }
  } catch (...) {
  }
  (void)now;
  ticket = UploadTicket{};
}

void Engine::release_uploads(const Connection& c) noexcept {
  try {
    if (c.upload != nullptr && (c.phase == Connection::Phase::upload || c.phase == Connection::Phase::hashing)) {
      // A POST that ends before its file is handed over: the session hears of it, and the place is free.
      upload_failed(c.upload_slot, protocol::ErrorCode::invalid_value, "The file did not arrive whole: add it again.",
                    Clock::now());
    }
    for (UploadTicket& ticket : uploads_) {
      if (ticket.live && ticket.session == c.serial) {
        ticket = UploadTicket{};  // the session that asked is gone: its ticket with it
      }
    }
  } catch (...) {
  }
}

}  // namespace detail
}  // namespace grace::serve
