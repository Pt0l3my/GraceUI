// abi_table.cpp: ABI 1's table (D73, D131), the slots behind GraceUiApi1. Each slot is noexcept and answers through a
// catch-all (D110); each copies what the mod hands it out of the mod's memory before anything else, a struct by the
// smaller of the two sizes and a string by its length within its bound, and hands grace-host GraceUI's own copies.
//
// SAFETY: the raw pointers from callers here are the slots' arguments: a struct a mod passes, read only after its first
// four bytes, its size, have been read, and only as far as that size (and this header's) reaches; a string, read only
// for the length the mod gives and only after that length has been checked against the string's bound; an array a
// struct names (a texture's pixels, a face's data and ranges, a draw list's vertices, indices and commands), read only
// for the counts the struct gives, after grace-host has checked them against their bounds, straight into GraceUI's own
// store; and a struct GraceUI fills, written only as far as the size the mod set in it. Nothing is kept that points
// into a mod's memory, but for an on_present callback and its `user` (set_on_present, D259), kept as they are and
// never read here: grace-host's draw.cpp calls the one with the other, under its rule (5).
#include "abi_table.h"

#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>

#include "grace/abi/drawlist.h"
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
static_assert(sizeof(GraceUiDrawCommand) == grace::abi::kDrawCommandSize10, "a command as ABI 1.0 first gave it");

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

// A struct GraceUI fills: the smaller of the mod's size and ours, its own size field left as the mod set it. Its size is
// read once, here, and what is written goes by that reading: a mod's other thread that changes the field meanwhile
// changes nothing GraceUI does (L6). False if `out` is null or its size is below `first`.
template <class T>
bool can_write(const T* out, std::uint32_t first, std::uint32_t& size) noexcept {
  static_assert(sizeof(std::uint32_t) <= sizeof(T));
  if (out == nullptr || first < sizeof size) {
    return false;
  }
  std::memcpy(&size, out, sizeof size);  // SAFETY: as above
  return size >= first;
}

// `size` is can_write's reading, at least `first`, which is at least the size field's own four bytes.
template <class T>
void write_out(T* out, std::uint32_t size, const T& value) noexcept {
  const std::size_t bytes = size < sizeof(T) ? size : sizeof(T);
  // SAFETY: can_write checked `out` and read `size`; the size field itself is not overwritten.
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
    if (grace::host::abi1::in_present_callback()) {
      return grace::host::abi1::refused_in_callback("connect");  // D259: no slot from inside a callback
    }
    std::uint32_t answer_size = 0;
    if (!can_write(answer, kConnectAnswer10, answer_size)) {
      grace::host::abi1::refused("connect", "an answer that is null or below its first size");
      return GRACEUI_REFUSED;
    }
    GraceUiConnectAnswer result{};
    result.graceui_version = kVersion;
    result.abi_minor = GRACEUI_ABI1_MINOR;
    grace::host::abi1::display_at_connect(result);  // ABI 1.2 (D180): written only as far as the mod's answer reaches
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
    write_out(answer, answer_size, result);
    return result.status;
  } catch (...) {
    grace::host::abi1::fault("connect");
    return GRACEUI_FAULT;
  }
}

GraceUiStatus disconnect(GraceUiConsumer consumer) noexcept {
  try {
    if (grace::host::abi1::in_present_callback()) {
      return grace::host::abi1::refused_in_callback("disconnect");  // D259: no slot from inside a callback
    }
    return grace::host::abi1::disconnect(consumer);
  } catch (...) {
    grace::host::abi1::fault("disconnect");
    return GRACEUI_FAULT;
  }
}

GraceUiStatus notice(GraceUiConsumer consumer, const GraceUiNotice* notice_in) noexcept {
  try {
    if (grace::host::abi1::in_present_callback()) {
      return grace::host::abi1::refused_in_callback("notice");  // D259: no slot from inside a callback
    }
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

// ---- S10.4.c's slots: textures, faces and glyphs, panels and draw lists, input and the world-state providers.

// A handle that names no mod answers so before anything about its arguments; otherwise a struct that cannot be read is
// refused, with one log line for the slot.
GraceUiStatus refused_argument(GraceUiConsumer consumer, const char* slot, const char* why) noexcept {
  const GraceUiStatus status = grace::host::abi1::not_yet(consumer);
  if (status == GRACEUI_NOT_CONNECTED || status == GRACEUI_INACTIVE) {
    return status;
  }
  grace::host::abi1::refused(slot, why);
  return GRACEUI_REFUSED;
}

constexpr std::uint32_t kTexture10 = 32;
constexpr std::uint32_t kFace10 = 40;
constexpr std::uint32_t kGlyphTable10 = 48;
constexpr std::uint32_t kPanel10 = 32;
constexpr std::uint32_t kDrawList10 = 48;
constexpr std::uint32_t kInputRequest10 = 24;
constexpr std::uint32_t kInputAnswer10 = 56;
constexpr std::uint32_t kWorldState10 = 8;

GraceUiStatus register_texture(GraceUiConsumer consumer, const GraceUiTextureDesc* texture,
                               std::uint64_t* texture_id) noexcept {
  try {
    if (grace::host::abi1::in_present_callback()) {
      return grace::host::abi1::refused_in_callback("register_texture");  // D259: no slot from inside a callback
    }
    GraceUiTextureDesc in{};
    if (texture_id == nullptr || !read_in(texture, kTexture10, in) || in.format != GRACEUI_TEXTURE_RGBA8 ||
        in.reserved != 0 || in.pixels == nullptr || in.width == 0 || in.height == 0 ||
        std::uint64_t{in.row_bytes} < std::uint64_t{in.width} * 4) {
      return refused_argument(consumer, "register_texture",
                              "a texture that is null, below its first size, not RGBA8, or rows shorter than its width");
    }
    std::uint64_t id = 0;
    const GraceUiStatus status = grace::host::abi1::register_texture(
        consumer, in.width, in.height,
        [](const void* context, unsigned char* out) {
          // SAFETY: `context` is the GraceUiTextureDesc copied above; the mod's pixels hold `height` rows of
          // `row_bytes` each, of which the first `width * 4` bytes are read; `out` has `width * height * 4` bytes.
          const auto& desc = *static_cast<const GraceUiTextureDesc*>(context);
          const auto* rows = static_cast<const unsigned char*>(desc.pixels);
          const std::size_t row = std::size_t{desc.width} * 4;
          for (std::uint32_t y = 0; y < desc.height; ++y) {
            std::memcpy(out + y * row, rows + std::size_t{y} * desc.row_bytes, row);
          }
        },
        &in, id);
    if (status == GRACEUI_OK) {
      *texture_id = id;  // SAFETY: checked not null above
    }
    return status;
  } catch (...) {
    grace::host::abi1::fault("register_texture");
    return GRACEUI_FAULT;
  }
}

GraceUiStatus register_face(GraceUiConsumer consumer, const GraceUiFaceDesc* face, std::uint32_t* face_id) noexcept {
  try {
    if (grace::host::abi1::in_present_callback()) {
      return grace::host::abi1::refused_in_callback("register_face");  // D259: no slot from inside a callback
    }
    GraceUiFaceDesc in{};
    if (face_id == nullptr || !read_in(face, kFace10, in) || in.data == nullptr ||
        (in.ranges == nullptr && in.range_count != 0) || (in.flags & ~std::uint32_t{GRACEUI_FACE_SCALED}) != 0) {
      return refused_argument(consumer, "register_face",
                              "a face that is null, below its first size, without data, or with a flag GraceUI does not "
                              "know");
    }
    std::uint32_t id = 0;
    // A face of 1.0's size is read with flags 0: baked at its pixel height, as before (D180).
    const GraceUiStatus status = grace::host::abi1::register_face(
        consumer, static_cast<float>(in.pixel_height), (in.flags & GRACEUI_FACE_SCALED) != 0, in.data_length,
        in.ranges == nullptr ? 0 : in.range_count,
        [](const void* context, unsigned char* data, std::uint32_t* ranges) {
          // SAFETY: `context` is the GraceUiFaceDesc copied above, whose length and range count GraceUI bounded before
          // it made room: the mod's data holds `data_length` bytes and its ranges `range_count` pairs.
          const auto& desc = *static_cast<const GraceUiFaceDesc*>(context);
          std::memcpy(data, desc.data, static_cast<std::size_t>(desc.data_length));
          if (ranges != nullptr) {
            std::memcpy(ranges, desc.ranges, static_cast<std::size_t>(desc.range_count) * 2 * sizeof(std::uint32_t));
          }
        },
        &in, id);
    if (status == GRACEUI_OK) {
      *face_id = id;  // SAFETY: checked not null above
    }
    return status;
  } catch (...) {
    grace::host::abi1::fault("register_face");
    return GRACEUI_FAULT;
  }
}

GraceUiStatus glyph_table(GraceUiConsumer consumer, std::uint32_t face_id, GraceUiGlyphTable* table) noexcept {
  try {
    if (grace::host::abi1::in_present_callback()) {
      return grace::host::abi1::refused_in_callback("glyph_table");  // D259: no slot from inside a callback
    }
    std::uint32_t table_size = 0;
    if (!can_write(table, kGlyphTable10, table_size)) {
      return refused_argument(consumer, "glyph_table", "an answer that is null or below its first size");
    }
    GraceUiGlyphTable result{};
    const GraceUiStatus status = grace::host::abi1::glyph_table(consumer, face_id, result);
    if (status == GRACEUI_OK) {
      write_out(table, table_size, result);
    }
    return status;
  } catch (...) {
    grace::host::abi1::fault("glyph_table");
    return GRACEUI_FAULT;
  }
}

GraceUiStatus create_panel(GraceUiConsumer consumer, const GraceUiPanelDesc* panel, std::uint64_t* panel_id) noexcept {
  try {
    if (grace::host::abi1::in_present_callback()) {
      return grace::host::abi1::refused_in_callback("create_panel");  // D259: no slot from inside a callback
    }
    GraceUiPanelDesc in{};
    if (panel_id == nullptr || !read_in(panel, kPanel10, in)) {
      return refused_argument(consumer, "create_panel", "a panel that is null or below its first size");
    }
    grace::abi::PanelDesc desc;
    desc.layer = in.layer;
    desc.anchor = in.anchor;
    desc.flags = in.flags;
    desc.x = in.x;
    desc.y = in.y;
    desc.width = in.width;
    desc.height = in.height;
    std::uint64_t id = 0;
    const GraceUiStatus status = grace::host::abi1::create_panel(consumer, desc, id);
    if (status == GRACEUI_OK) {
      *panel_id = id;  // SAFETY: checked not null above
    }
    return status;
  } catch (...) {
    grace::host::abi1::fault("create_panel");
    return GRACEUI_FAULT;
  }
}

// What submit's copy reads: the list's header, copied, and the mod's stride.
struct ListSource {
  GraceUiDrawList list;
};

GraceUiStatus submit(GraceUiConsumer consumer, std::uint64_t panel_id, const GraceUiDrawList* list) noexcept {
  try {
    if (grace::host::abi1::in_present_callback()) {
      return grace::host::abi1::refused_in_callback("submit");  // D259: no slot from inside a callback
    }
    ListSource source{};
    if (!read_in(list, kDrawList10, source.list)) {
      return refused_argument(consumer, "submit", "a draw list that is null or below its first size");
    }
    const GraceUiDrawList& in = source.list;
    grace::host::abi1::ListShape shape;
    shape.vertex_count = in.vertex_count;
    shape.index_count = in.index_count;
    shape.command_count = in.command_count;
    shape.command_size = in.command_size;
    shape.atlas_generation = in.atlas_generation;
    shape.arrays = (in.vertex_count == 0 || in.vertices != nullptr) && (in.index_count == 0 || in.indices != nullptr) &&
                   (in.command_count == 0 || in.commands != nullptr);
    return grace::host::abi1::submit(
        consumer, panel_id, shape,
        [](const void* context, grace::abi::DrawListData& out) {
          // SAFETY: `context` is the ListSource above. GraceUI checked the counts against their bounds and the arrays
          // against null, and sized `out`'s vectors to the counts, before this runs: the mod's arrays hold that many
          // vertices and indices, and that many commands of `command_size` bytes each, of which the first
          // min(command_size, sizeof(GraceUiDrawCommand)) are read, the rest of each of GraceUI's left zero.
          const GraceUiDrawList& from = static_cast<const ListSource*>(context)->list;
          if (!out.vertices.empty()) {
            std::memcpy(out.vertices.data(), from.vertices, out.vertices.size() * sizeof(GraceUiDrawVert));
          }
          if (!out.indices.empty()) {
            std::memcpy(out.indices.data(), from.indices, out.indices.size() * sizeof(std::uint16_t));
          }
          const auto* commands = static_cast<const unsigned char*>(static_cast<const void*>(from.commands));
          const std::size_t read = from.command_size < sizeof(GraceUiDrawCommand) ? from.command_size
                                                                                  : sizeof(GraceUiDrawCommand);
          for (std::size_t i = 0; i < out.commands.size(); ++i) {
            out.commands[i] = GraceUiDrawCommand{};
            std::memcpy(&out.commands[i], commands + i * from.command_size, read);
          }
        },
        &source);
  } catch (...) {
    grace::host::abi1::fault("submit");
    return GRACEUI_FAULT;
  }
}

GraceUiStatus show_panel(GraceUiConsumer consumer, std::uint64_t panel_id) noexcept {
  try {
    if (grace::host::abi1::in_present_callback()) {
      return grace::host::abi1::refused_in_callback("show_panel");  // D259: no slot from inside a callback
    }
    return grace::host::abi1::show_panel(consumer, panel_id, true);
  } catch (...) {
    grace::host::abi1::fault("show_panel");
    return GRACEUI_FAULT;
  }
}

GraceUiStatus hide_panel(GraceUiConsumer consumer, std::uint64_t panel_id) noexcept {
  try {
    if (grace::host::abi1::in_present_callback()) {
      return grace::host::abi1::refused_in_callback("hide_panel");  // D259: no slot from inside a callback
    }
    return grace::host::abi1::show_panel(consumer, panel_id, false);
  } catch (...) {
    grace::host::abi1::fault("hide_panel");
    return GRACEUI_FAULT;
  }
}

GraceUiStatus request_input(GraceUiConsumer consumer, const GraceUiInputRequest* request,
                            GraceUiInputAnswer* answer) noexcept {
  try {
    if (grace::host::abi1::in_present_callback()) {
      return grace::host::abi1::refused_in_callback("request_input");  // D259: no slot from inside a callback
    }
    GraceUiInputRequest in{};
    std::uint32_t answer_size = 0;
    if (!can_write(answer, kInputAnswer10, answer_size) || !read_in(request, kInputRequest10, in)) {
      return refused_argument(consumer, "request_input", "a request or answer that is null or below its first size");
    }
    GraceUiInputAnswer result{};
    // A 1.0 request is read with flags 0: its mod hears nothing of its input, as before (D176).
    const GraceUiStatus status =
        grace::host::abi1::request_input(consumer, in.kind, in.wants, in.panel, in.flags, result);
    if (status == GRACEUI_OK) {
      write_out(answer, answer_size, result);
    }
    return status;
  } catch (...) {
    grace::host::abi1::fault("request_input");
    return GRACEUI_FAULT;
  }
}

GraceUiStatus release_input(GraceUiConsumer consumer) noexcept {
  try {
    if (grace::host::abi1::in_present_callback()) {
      return grace::host::abi1::refused_in_callback("release_input");  // D259: no slot from inside a callback
    }
    return grace::host::abi1::release_input(consumer);
  } catch (...) {
    grace::host::abi1::fault("release_input");
    return GRACEUI_FAULT;
  }
}

GraceUiStatus set_world_state(GraceUiConsumer consumer, const GraceUiWorldState* state) noexcept {
  try {
    if (grace::host::abi1::in_present_callback()) {
      return grace::host::abi1::refused_in_callback("set_world_state");  // D259: no slot from inside a callback
    }
    GraceUiWorldState in{};
    if (!read_in(state, kWorldState10, in)) {
      return refused_argument(consumer, "set_world_state", "a state that is null or below its first size");
    }
    return grace::host::abi1::set_world_state(consumer, in.flags);
  } catch (...) {
    grace::host::abi1::fault("set_world_state");
    return GRACEUI_FAULT;
  }
}

// ---- D155's slots: hotkeys, and poll's hotkey events.

constexpr std::uint32_t kHotkey10 = 48;
constexpr std::uint32_t kEvent10 = 16;
constexpr std::uint32_t kEvent02 = 48;       // with reply, frame, frame_capacity and frame_length (D169)
constexpr std::uint32_t kEvent11 = 104;      // with a holder's fields, ABI 1.1 (D176)
constexpr std::uint32_t kEvent12 = 136;      // with the display's fields, ABI 1.2 (D180)
constexpr std::uint32_t kEvent16 = 160;      // with a file's fields, ABI 1.6 (D226)
constexpr std::uint32_t kEvent19 = 168;      // with Plain text, ABI 1.9 (D268)
constexpr std::uint32_t kSpaceDesc02 = 64;   // GraceUiSpaceDesc as GraceUI 0.2 first gave it
constexpr std::uint32_t kCaret11 = 32;       // GraceUiCaret as ABI 1.1 first gave it
constexpr std::uint64_t kPagesMax = 1u << 20;  // a space's page data or settings description: 1 MiB (EST)
static_assert(sizeof(GraceUiEvent) == kEvent19 && sizeof(GraceUiSpaceDesc) == kSpaceDesc02 &&
              sizeof(GraceUiCaret) == kCaret11);

GraceUiStatus register_hotkey(GraceUiConsumer consumer, const GraceUiHotkeyDesc* hotkey,
                              std::uint32_t* hotkey_id) noexcept {
  try {
    if (grace::host::abi1::in_present_callback()) {
      return grace::host::abi1::refused_in_callback("register_hotkey");  // D259: no slot from inside a callback
    }
    GraceUiHotkeyDesc in{};
    char action[GRACEUI_ID_MAX];
    char name[GRACEUI_NAME_MAX];
    std::string_view action_view;
    std::string_view name_view;
    if (hotkey_id == nullptr || !read_in(hotkey, kHotkey10, in) ||
        !read_text(in.action, in.action_length, action, action_view) ||
        !read_text(in.name, in.name_length, name, name_view)) {
      return refused_argument(consumer, "register_hotkey",
                              "a hotkey that is null, below its first size, or with an action or name too long");
    }
    std::uint32_t id = 0;
    const GraceUiStatus status = grace::host::abi1::register_hotkey(consumer, in.key, in.modifiers, in.pad_chord,
                                                                    action_view, name_view, id);
    if (status == GRACEUI_OK || status == GRACEUI_UNBOUND) {
      *hotkey_id = id;  // SAFETY: checked not null above
    }
    return status;
  } catch (...) {
    grace::host::abi1::fault("register_hotkey");
    return GRACEUI_FAULT;
  }
}

GraceUiStatus poll(GraceUiConsumer consumer, GraceUiEvent* event) noexcept {
  try {
    if (grace::host::abi1::in_present_callback()) {
      return grace::host::abi1::refused_in_callback("poll");  // D259: no slot from inside a callback
    }
    std::uint32_t event_size = 0;
    if (!can_write(event, kEvent10, event_size)) {
      return refused_argument(consumer, "poll", "an event that is null or below its first size");
    }
    GraceUiEvent out{};
    out.size = static_cast<std::uint32_t>(sizeof out);
    // From 0.2 (D169): an event that holds the frame's fields takes the space's events too, into the mod's buffer.
    const bool protocol = event_size >= kEvent02;
    if (protocol) {
      GraceUiEvent in{};
      read_in(event, kEvent02, in);
      out.frame = in.frame;
      out.frame_capacity = in.frame == nullptr ? 0 : in.frame_capacity;
      if (event_size >= kEvent16) {
        out.file = in.file;  // ABI 1.6 (D226): the mod's buffer for a file command's file
        out.file_capacity = in.file == nullptr ? 0 : in.file_capacity;
      }
    }
    // ABI 1.1 (D176): an event that holds a holder's fields takes the mod's holder's events too.
    // ABI 1.2 (D180): one of 1.2's size takes the display's event too.
    const GraceUiStatus status =
        grace::host::abi1::poll(consumer, out, protocol, event_size >= kEvent11, event_size >= kEvent12,
                                event_size >= kEvent16);
    if (status == GRACEUI_OK || status == GRACEUI_FULL) {
      write_out(event, event_size, out);
    }
    return status;
  } catch (...) {
    grace::host::abi1::fault("poll");
    return GRACEUI_FAULT;
  }
}

// ---- The slots later increments build: not connected, inactive, or not yet.

GraceUiStatus not_yet(GraceUiConsumer consumer, const char* slot) noexcept {
  try {
    return grace::host::abi1::not_yet(consumer);
  } catch (...) {
    grace::host::abi1::fault(slot);
    return GRACEUI_FAULT;
  }
}

// ---- D169's slots: a mod's space, the bridge protocol's frames across the ABI.

// A mod's JSON, seen whole within the call: null with a length of 0 is empty; longer than `limit` fails.
bool frame_text(const char* text, std::uint64_t length, std::uint64_t limit, std::string_view& out) noexcept {
  if (length > limit || (text == nullptr && length != 0)) {
    return false;
  }
  out = length == 0 ? std::string_view() : std::string_view(text, static_cast<std::size_t>(length));
  return true;
}

GraceUiStatus register_pages(GraceUiConsumer consumer, const GraceUiSpaceDesc* space) noexcept {
  try {
    if (grace::host::abi1::in_present_callback()) {
      return grace::host::abi1::refused_in_callback("register_pages");  // D259: no slot from inside a callback
    }
    GraceUiSpaceDesc in{};
    std::string_view pages;
    std::string_view settings;
    std::string_view increment;
    if (!read_in(space, kSpaceDesc02, in) || !frame_text(in.pages, in.pages_length, kPagesMax, pages) ||
        !frame_text(in.settings, in.settings_length, kPagesMax, settings) ||
        !frame_text(in.increment, in.increment_length, 16, increment)) {
      return refused_argument(consumer, "register_pages", "a space that is null, below its first size, or too long");
    }
    // Copied before the registry parses them: a mod's other thread that changes its buffers meanwhile changes nothing.
    const std::string pages_copy(pages);
    const std::string settings_copy(settings);
    const std::string increment_copy(increment);
    return grace::host::abi1::register_pages(consumer, in.views, in.version, pages_copy, settings_copy, increment_copy);
  } catch (...) {
    grace::host::abi1::fault("register_pages");
    return GRACEUI_FAULT;
  }
}

GraceUiStatus publish(GraceUiConsumer consumer, const char* frame, std::uint64_t frame_length) noexcept {
  try {
    if (grace::host::abi1::in_present_callback()) {
      return grace::host::abi1::refused_in_callback("publish");  // D259: no slot from inside a callback
    }
    std::string_view text;
    if (!frame_text(frame, frame_length, GRACEUI_FRAME_TO_PAGE_MAX, text) || text.empty()) {
      return refused_argument(consumer, "publish", "a frame that is null, empty or too long");
    }
    const std::string copy(text);
    return grace::host::abi1::publish(consumer, copy);
  } catch (...) {
    grace::host::abi1::fault("publish");
    return GRACEUI_FAULT;
  }
}

GraceUiStatus answer(GraceUiConsumer consumer, std::uint64_t reply, const char* frame,
                     std::uint64_t frame_length) noexcept {
  try {
    if (grace::host::abi1::in_present_callback()) {
      return grace::host::abi1::refused_in_callback("answer");  // D259: no slot from inside a callback
    }
    std::string_view text;
    if (!frame_text(frame, frame_length, GRACEUI_FRAME_TO_PAGE_MAX, text) || text.empty()) {
      return refused_argument(consumer, "answer", "a frame that is null, empty or too long");
    }
    const std::string copy(text);
    return grace::host::abi1::answer(consumer, reply, copy);
  } catch (...) {
    grace::host::abi1::fault("answer");
    return GRACEUI_FAULT;
  }
}
GraceUiStatus log(GraceUiConsumer consumer, std::uint32_t, const char*, std::uint64_t) noexcept {
  if (grace::host::abi1::in_present_callback()) {
    return grace::host::abi1::refused_in_callback("log");  // D259: no slot from inside a callback
  }
  return not_yet(consumer, "log");
}

// ---- ABI 1.1's slot (D176).

GraceUiStatus set_caret(GraceUiConsumer consumer, const GraceUiCaret* caret) noexcept {
  try {
    if (grace::host::abi1::in_present_callback()) {
      return grace::host::abi1::refused_in_callback("set_caret");  // D259: no slot from inside a callback
    }
    GraceUiCaret in{};
    if (!read_in(caret, kCaret11, in)) {
      return refused_argument(consumer, "set_caret", "a caret that is null or below its first size");
    }
    return grace::host::abi1::set_caret(consumer, in.panel, in.x, in.y, in.height);
  } catch (...) {
    grace::host::abi1::fault("set_caret");
    return GRACEUI_FAULT;
  }
}

// ---- ABI 1.4's slot (D210).

GraceUiStatus describe_hotkey(GraceUiConsumer consumer, std::uint32_t hotkey_id, GraceUiHotkeyBinding* binding) noexcept {
  try {
    if (grace::host::abi1::in_present_callback()) {
      return grace::host::abi1::refused_in_callback("describe_hotkey");  // D259: no slot from inside a callback
    }
    if (binding == nullptr || binding->size < sizeof(GraceUiHotkeyBinding)) {
      return refused_argument(consumer, "describe_hotkey", "a binding that is null or below 1.4's size");
    }
    GraceUiHotkeyBinding out{};
    out.size = binding->size;
    const GraceUiStatus status = grace::host::abi1::describe_hotkey(consumer, hotkey_id, out);
    if (status == GRACEUI_OK) {
      out.size = binding->size;
      *binding = out;  // 1.4's size, which the check above guarantees the caller's struct has
    }
    return status;
  } catch (...) {
    grace::host::abi1::fault("describe_hotkey");
    return GRACEUI_FAULT;
  }
}

// ---- ABI 1.5's slot (D212).

GraceUiStatus wake_event(GraceUiConsumer consumer, void** event) noexcept {
  try {
    if (grace::host::abi1::in_present_callback()) {
      return grace::host::abi1::refused_in_callback("wake_event");  // D259: no slot from inside a callback
    }
    if (event == nullptr) {
      return refused_argument(consumer, "wake_event", "a null place for the handle");
    }
    void* handle = nullptr;
    const GraceUiStatus status = grace::host::abi1::wake_event(consumer, handle);
    *event = status == GRACEUI_OK ? handle : nullptr;
    return status;
  } catch (...) {
    grace::host::abi1::fault("wake_event");
    return GRACEUI_FAULT;
  }
}

// ---- ABI 1.8's slot (D259).

constexpr std::uint32_t kPresentHook18 = 32;
static_assert(sizeof(GraceUiPresentHook) >= kPresentHook18);

GraceUiStatus set_on_present(GraceUiConsumer consumer, std::uint64_t panel_id, const GraceUiPresentHook* hook) noexcept {
  try {
    if (grace::host::abi1::in_present_callback()) {
      return grace::host::abi1::refused_in_callback("set_on_present");  // D259: no slot from inside a callback
    }
    GraceUiPresentHook in{};
    if (!read_in(hook, kPresentHook18, in)) {
      return refused_argument(consumer, "set_on_present", "a hook that is null or below 1.8's size");
    }
    grace::abi::PresentHookDesc desc;
    desc.callback = in.callback;  // SAFETY: a pointer kept, never read here: rule (5) of grace-host's draw.cpp calls it
    desc.user = in.user;
    if (in.callback != nullptr) {
      desc.vertex_capacity = in.vertex_capacity;
      desc.index_capacity = in.index_capacity;
      desc.command_capacity = in.command_capacity;
    }
    return grace::host::abi1::set_on_present(consumer, panel_id, desc);
  } catch (...) {
    grace::host::abi1::fault("set_on_present");
    return GRACEUI_FAULT;
  }
}

// ---- ABI 1.10's slot (D276).

constexpr std::uint32_t kPadKeyboard110 = 32;  // GraceUiPadKeyboard as ABI 1.10 first gave it
static_assert(sizeof(GraceUiPadKeyboard) >= kPadKeyboard110);

GraceUiStatus pad_keyboard(GraceUiConsumer consumer, const GraceUiPadKeyboard* keyboard) noexcept {
  try {
    if (grace::host::abi1::in_present_callback()) {
      return grace::host::abi1::refused_in_callback("pad_keyboard");  // D259: no slot from inside a callback
    }
    if (keyboard == nullptr) {
      return grace::host::abi1::pad_keyboard(consumer, false, 0, 0, 0, 0, 0, 0);  // closes the mod's
    }
    GraceUiPadKeyboard in{};
    if (!read_in(keyboard, kPadKeyboard110, in)) {
      return refused_argument(consumer, "pad_keyboard", "a keyboard below its first size");
    }
    return grace::host::abi1::pad_keyboard(consumer, true, in.mode, in.panel, in.x, in.y, in.width, in.height);
  } catch (...) {
    grace::host::abi1::fault("pad_keyboard");
    return GRACEUI_FAULT;
  }
}

// ---- ABI 1.11's slot (D280).

GraceUiStatus set_hotkey_enabled(GraceUiConsumer consumer, std::uint32_t hotkey_id, std::uint32_t enabled) noexcept {
  try {
    if (grace::host::abi1::in_present_callback()) {
      return grace::host::abi1::refused_in_callback("set_hotkey_enabled");  // D259: no slot from inside a callback
    }
    return grace::host::abi1::set_hotkey_enabled(consumer, hotkey_id, enabled != 0);
  } catch (...) {
    grace::host::abi1::fault("set_hotkey_enabled");
    return GRACEUI_FAULT;
  }
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
    &set_caret,
    &describe_hotkey,
    &wake_event,
    &set_on_present,
    &pad_keyboard,
    &set_hotkey_enabled,
};

}  // namespace

const GraceUiApi1* table1() noexcept { return &kTable; }

}  // namespace graceui
