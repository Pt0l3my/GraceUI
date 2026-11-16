// abi_table.cpp: ABI 1's table (D73, D131), the slots behind GraceUiApi1. Each slot is noexcept and answers through a
// catch-all (D110); each copies what the mod hands it out of the mod's memory before anything else, a struct by the
// smaller of the two sizes and a string by its length within its bound, and hands grace-host GraceUI's own copies.
//
// SAFETY: the raw pointers from callers here are the slots' arguments: a struct a mod passes, read only after its first
// four bytes, its size, have been read, and only as far as that size (and this header's) reaches; a string, read only
// for the length the mod gives and only after that length has been checked against the string's bound; and a struct
// GraceUI fills, written only as far as the size the mod set in it. Nothing is kept that points into a mod's memory.
#include "abi_table.h"

#include <cstdint>
#include <cstring>
#include <string_view>

#include "grace/host/host.h"
#include "graceui.h"

namespace graceui {
namespace {

constexpr std::uint32_t kVersion = GRACEUI_VERSION_PACK(GRACEUI_BUILD_MAJOR, GRACEUI_BUILD_MINOR, GRACEUI_BUILD_PATCH);

// The sizes ABI 1.0 first gave the structs the slots built so far take: one below is refused (D73).
constexpr std::uint32_t kConnectRequest10 = 64;
constexpr std::uint32_t kConnectAnswer10 = 32;
constexpr std::uint32_t kNotice10 = 32;
static_assert(sizeof(GraceUiConnectRequest) >= kConnectRequest10 && sizeof(GraceUiConnectAnswer) >= kConnectAnswer10 &&
              sizeof(GraceUiNotice) >= kNotice10);

// A mod's struct, read as D73 says: the smaller of its size and ours, the rest zero; false if `in` is null or its size
// is below `first`.
template <class T>
bool read_in(const T* in, std::uint32_t first, T& out) noexcept {
  if (in == nullptr) {
    return false;
  }
  std::uint32_t size = 0;
  std::memcpy(&size, in, sizeof size);  // SAFETY: every struct a mod passes starts with its size
  if (size < first) {
    return false;
  }
  out = T{};
  std::memcpy(&out, in, size < sizeof(T) ? size : sizeof(T));  // SAFETY: the mod's size says this much is there
  out.size = static_cast<std::uint32_t>(sizeof(T));
  return true;
}

// A struct GraceUI fills: the smaller of the mod's size and ours, its own size field left as the mod set it; false if
// `out` is null or its size is below `first`.
template <class T>
bool can_write(const T* out, std::uint32_t first) noexcept {
  if (out == nullptr) {
    return false;
  }
  std::uint32_t size = 0;
  std::memcpy(&size, out, sizeof size);  // SAFETY: as above
  return size >= first;
}

template <class T>
void write_out(T* out, const T& value) noexcept {
  std::uint32_t size = 0;
  std::memcpy(&size, out, sizeof size);
  const std::size_t bytes = size < sizeof(T) ? size : sizeof(T);
  // SAFETY: can_write checked `out` and its size; the size field itself is not overwritten.
  std::memcpy(reinterpret_cast<unsigned char*>(out) + sizeof size,
              reinterpret_cast<const unsigned char*>(&value) + sizeof size, bytes - sizeof size);
}

// A mod's string: copied into `buffer` if it is no longer than `N`, and seen as a view of that copy. A null pointer
// with a length of 0 is an empty string.
template <std::size_t N>
bool read_text(const char* text, std::uint64_t length, char (&buffer)[N], std::string_view& out) noexcept {
  if (length > N || (text == nullptr && length != 0)) {
    return false;
  }
  if (length != 0) {
    std::memcpy(buffer, text, static_cast<std::size_t>(length));  // SAFETY: `length` bytes, checked against N
  }
  out = std::string_view(buffer, static_cast<std::size_t>(length));
  return true;
}

// ---- The slots GraceUI 0.1 provides.

std::uint32_t has_cap(const char* name, std::uint64_t name_length) noexcept {
  try {
    char buffer[GRACEUI_ID_MAX];
    std::string_view view;
    return read_text(name, name_length, buffer, view) && grace::host::abi1::has_cap(view) ? 1u : 0u;
  } catch (...) {
    grace::host::abi1::fault("has_cap");
    return 0;
  }
}

GraceUiStatus connect(const GraceUiConnectRequest* request, GraceUiConnectAnswer* answer) noexcept {
  try {
    if (!can_write(answer, kConnectAnswer10)) {
      grace::host::abi1::refused("connect", "an answer that is null or below its first size");
      return GRACEUI_REFUSED;
    }
    GraceUiConnectAnswer result{};
    result.graceui_version = kVersion;
    result.abi_minor = GRACEUI_ABI1_MINOR;
    GraceUiConnectRequest in{};
    char id[GRACEUI_ID_MAX];
    char name[GRACEUI_NAME_MAX];
    char caps[GRACEUI_CAPS_MAX];
    grace::abi::ConnectRequest copy;
    // Refused before the rules see it: a request below its first size, or a string longer than its bound.
    result.status = GRACEUI_REFUSED;
    if (!read_in(request, kConnectRequest10, in)) {
      result.reason = GRACEUI_REFUSED_ARGUMENT;
    } else if (!read_text(in.id, in.id_length, id, copy.id)) {
      result.reason = GRACEUI_REFUSED_ID;
    } else if (!read_text(in.name, in.name_length, name, copy.name)) {
      result.reason = GRACEUI_REFUSED_NAME;
    } else if (!read_text(in.caps, in.caps_length, caps, copy.caps)) {
      result.reason = GRACEUI_REFUSED_CAPS;
    }
    if (result.reason != 0) {
      grace::host::abi1::connect_refused(result.reason);
    } else {
      copy.abi_minor = in.abi_minor;
      copy.version = in.version;
      copy.floor = in.floor;
      const grace::abi::ConnectResult connected = grace::host::abi1::connect(copy);
      result.status = connected.status;
      result.reason = connected.reason;
      result.consumer = connected.handle;
      result.floor = connected.status == GRACEUI_TOO_OLD ? in.floor : 0;
    }
    write_out(answer, result);
    return result.status;
  } catch (...) {
    grace::host::abi1::fault("connect");
    return GRACEUI_FAULT;
  }
}

GraceUiStatus disconnect(GraceUiConsumer consumer) noexcept {
  try {
    return grace::host::abi1::disconnect(consumer);
  } catch (...) {
    grace::host::abi1::fault("disconnect");
    return GRACEUI_FAULT;
  }
}

GraceUiStatus notice(GraceUiConsumer consumer, const GraceUiNotice* notice_in) noexcept {
  try {
    GraceUiNotice in{};
    char text[GRACEUI_NOTICE_TEXT_MAX];
    std::string_view view;
    if (!read_in(notice_in, kNotice10, in) || !read_text(in.text, in.text_length, text, view)) {
      // A handle that names no mod answers so before anything about its arguments.
      const GraceUiStatus status = grace::host::abi1::not_yet(consumer);
      if (status == GRACEUI_NOT_CONNECTED) {
        return status;
      }
      grace::host::abi1::refused("notice", "a notice that is null, below its first size, or longer than 512 bytes");
      return GRACEUI_REFUSED;
    }
    return grace::host::abi1::notice(consumer, in.tone, in.key, in.duration_ms, view);
  } catch (...) {
    grace::host::abi1::fault("notice");
    return GRACEUI_FAULT;
  }
}

// ---- The slots the next increments build: not connected, inactive, or not yet.

GraceUiStatus not_yet(GraceUiConsumer consumer, const char* slot) noexcept {
  try {
    return grace::host::abi1::not_yet(consumer);
  } catch (...) {
    grace::host::abi1::fault(slot);
    return GRACEUI_FAULT;
  }
}

GraceUiStatus register_pages(GraceUiConsumer consumer, const void*) noexcept { return not_yet(consumer, "register_pages"); }
GraceUiStatus publish(GraceUiConsumer consumer, const void*) noexcept { return not_yet(consumer, "publish"); }
GraceUiStatus poll(GraceUiConsumer consumer, const void*) noexcept { return not_yet(consumer, "poll"); }
GraceUiStatus answer(GraceUiConsumer consumer, const void*) noexcept { return not_yet(consumer, "answer"); }

GraceUiStatus register_texture(GraceUiConsumer consumer, const GraceUiTextureDesc*, std::uint64_t*) noexcept {
  return not_yet(consumer, "register_texture");
}
GraceUiStatus register_face(GraceUiConsumer consumer, const GraceUiFaceDesc*, std::uint32_t*) noexcept {
  return not_yet(consumer, "register_face");
}
GraceUiStatus glyph_table(GraceUiConsumer consumer, std::uint32_t, GraceUiGlyphTable*) noexcept {
  return not_yet(consumer, "glyph_table");
}
GraceUiStatus create_panel(GraceUiConsumer consumer, const GraceUiPanelDesc*, std::uint64_t*) noexcept {
  return not_yet(consumer, "create_panel");
}
GraceUiStatus submit(GraceUiConsumer consumer, std::uint64_t, const GraceUiDrawList*) noexcept {
  return not_yet(consumer, "submit");
}
GraceUiStatus show_panel(GraceUiConsumer consumer, std::uint64_t) noexcept { return not_yet(consumer, "show_panel"); }
GraceUiStatus hide_panel(GraceUiConsumer consumer, std::uint64_t) noexcept { return not_yet(consumer, "hide_panel"); }
GraceUiStatus request_input(GraceUiConsumer consumer, const GraceUiInputRequest*, GraceUiInputAnswer*) noexcept {
  return not_yet(consumer, "request_input");
}
GraceUiStatus release_input(GraceUiConsumer consumer) noexcept { return not_yet(consumer, "release_input"); }
GraceUiStatus register_hotkey(GraceUiConsumer consumer, const GraceUiHotkeyDesc*, std::uint32_t*) noexcept {
  return not_yet(consumer, "register_hotkey");
}
GraceUiStatus set_world_state(GraceUiConsumer consumer, const GraceUiWorldState*) noexcept {
  return not_yet(consumer, "set_world_state");
}
GraceUiStatus log(GraceUiConsumer consumer, std::uint32_t, const char*, std::uint64_t) noexcept {
  return not_yet(consumer, "log");
}

constinit const GraceUiApi1 kTable = {
    sizeof(GraceUiApi1),
    GRACEUI_ABI1_MINOR,
    kVersion,
    0,
    &has_cap,
    &connect,
    &disconnect,
    &register_pages,
    &publish,
    &poll,
    &answer,
    &register_texture,
    &register_face,
    &glyph_table,
    &create_panel,
    &submit,
    &show_panel,
    &hide_panel,
    &request_input,
    &release_input,
    &register_hotkey,
    &notice,
    &set_world_state,
    &log,
};

}  // namespace

const GraceUiApi1* table1() noexcept { return &kTable; }

}  // namespace graceui
