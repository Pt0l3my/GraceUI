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
// into a mod's memory.
#include "abi_table.h"

#include <cstdint>
#include <cstring>
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
    std::uint32_t answer_size = 0;
    if (!can_write(answer, kConnectAnswer10, answer_size)) {
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
    write_out(answer, answer_size, result);
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
    GraceUiFaceDesc in{};
    if (face_id == nullptr || !read_in(face, kFace10, in) || in.data == nullptr ||
        (in.ranges == nullptr && in.range_count != 0)) {
      return refused_argument(consumer, "register_face", "a face that is null, below its first size, or without data");
    }
    std::uint32_t id = 0;
    const GraceUiStatus status = grace::host::abi1::register_face(
        consumer, static_cast<float>(in.pixel_height), in.data_length, in.ranges == nullptr ? 0 : in.range_count,
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
    return grace::host::abi1::show_panel(consumer, panel_id, true);
  } catch (...) {
    grace::host::abi1::fault("show_panel");
    return GRACEUI_FAULT;
  }
}

GraceUiStatus hide_panel(GraceUiConsumer consumer, std::uint64_t panel_id) noexcept {
  try {
    return grace::host::abi1::show_panel(consumer, panel_id, false);
  } catch (...) {
    grace::host::abi1::fault("hide_panel");
    return GRACEUI_FAULT;
  }
}

GraceUiStatus request_input(GraceUiConsumer consumer, const GraceUiInputRequest* request,
                            GraceUiInputAnswer* answer) noexcept {
  try {
    GraceUiInputRequest in{};
    std::uint32_t answer_size = 0;
    if (!can_write(answer, kInputAnswer10, answer_size) || !read_in(request, kInputRequest10, in)) {
      return refused_argument(consumer, "request_input", "a request or answer that is null or below its first size");
    }
    GraceUiInputAnswer result{};
    const GraceUiStatus status = grace::host::abi1::request_input(consumer, in.kind, in.wants, in.panel, result);
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
    return grace::host::abi1::release_input(consumer);
  } catch (...) {
    grace::host::abi1::fault("release_input");
    return GRACEUI_FAULT;
  }
}

GraceUiStatus set_world_state(GraceUiConsumer consumer, const GraceUiWorldState* state) noexcept {
  try {
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

GraceUiStatus register_hotkey(GraceUiConsumer consumer, const GraceUiHotkeyDesc* hotkey,
                              std::uint32_t* hotkey_id) noexcept {
  try {
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
    if (status == GRACEUI_OK) {
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
    std::uint32_t event_size = 0;
    if (!can_write(event, kEvent10, event_size)) {
      return refused_argument(consumer, "poll", "an event that is null or below its first size");
    }
    GraceUiEvent out{};
    out.size = static_cast<std::uint32_t>(sizeof out);
    const GraceUiStatus status = grace::host::abi1::poll(consumer, out);
    if (status == GRACEUI_OK) {
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

GraceUiStatus register_pages(GraceUiConsumer consumer, const void*) noexcept { return not_yet(consumer, "register_pages"); }
GraceUiStatus publish(GraceUiConsumer consumer, const void*) noexcept { return not_yet(consumer, "publish"); }
GraceUiStatus answer(GraceUiConsumer consumer, const void*) noexcept { return not_yet(consumer, "answer"); }
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
