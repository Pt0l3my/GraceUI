// drawlist.h: a panel's draw lists (D76): GraceUI's copy of what a mod submits, the checker every list passes before it
// can be drawn, and the triple buffer the present thread reads the newest list from. Pure: no Win32, no Dear ImGui.
//
// A list is copied on the mod's thread into the panel's back buffer, checked there (a check of GraceUI's own copy, which
// the mod cannot change while it is checked) and published only if it passes. The present thread takes the newest
// published list and draws it; it never waits, never allocates and never frees (invariant 10). A triple buffer's back
// buffer is never the reader's, so the writer grows it, or frees what it held, on its own thread at once: the retire
// slot §2.9 describes for a shared snapshot is not needed here (D137).
#pragma once

#include <atomic>
#include <cstdint>
#include <vector>

#include "grace/abi/spin.h"
#include "graceui.h"

namespace grace::abi {

// The bounds of one submission (D76, EST): 256 KiB of vertices, 256 KiB of indices and 64 commands; a command's
// vertices at most 65,536, as u16 indices reach.
inline constexpr std::uint32_t kDrawVertexBytesMax = GRACEUI_DRAW_VERTEX_BYTES_MAX;
inline constexpr std::uint32_t kDrawIndexBytesMax = GRACEUI_DRAW_INDEX_BYTES_MAX;
inline constexpr std::uint32_t kDrawVerticesMax = kDrawVertexBytesMax / sizeof(GraceUiDrawVert);
inline constexpr std::uint32_t kDrawIndicesMax = kDrawIndexBytesMax / sizeof(std::uint16_t);
inline constexpr std::uint32_t kDrawCommandsMax = GRACEUI_DRAW_COMMANDS_MAX;
inline constexpr std::uint32_t kCommandVerticesMax = 65536;
// A command as ABI 1.0 first gave it: a mod's stride below this is refused.
inline constexpr std::uint32_t kDrawCommandSize10 = 40;

// Why the checker refused a list; none if it passed.
enum class Refusal : std::uint8_t {
  none = 0,
  too_large = 1,     // more vertices, indices or commands than kDraw*Max, or a stride below the first command's size
  vertex_range = 2,  // a command's vertices outside the list's, or more than kCommandVerticesMax of them
  index_range = 3,   // a command's indices outside the list's, or not whole triangles
  index = 4,         // an index beyond its command's own vertices
  clip = 5,          // a clip rectangle not finite, inverted, or outside the panel's bounds
  texture = 6,       // a texture neither the mod's own nor the shared atlas
  generation = 7,    // the shared atlas at another generation than the list names, or before the atlas is baked
};

const char* refusal_name(Refusal refusal) noexcept;

// GraceUI's copy of one list, in the panel's own pixels (its top-left corner at 0,0).
struct DrawListData {
  std::vector<GraceUiDrawVert> vertices;
  std::vector<std::uint16_t> indices;
  std::vector<GraceUiDrawCommand> commands;
  std::uint32_t atlas_generation = 0;
  std::uint32_t panel_generation = 0;  // the panel it was submitted to (panels.h): a reused slot draws no older list
  std::uint64_t serial = 0;            // 0: never written
  // The reader's own: where its vertices now are, in the display's pixels (it moves them where the panel is placed).
  float placed_x = 0.0f;
  float placed_y = 0.0f;
};

// What the checker weighs a list against.
struct DrawContext {
  float width = 0.0f;   // the panel's bounds: every clip rectangle within [0, width] x [0, height]
  float height = 0.0f;
  const std::uint64_t* textures = nullptr;  // the mod's registered textures
  std::uint32_t texture_count = 0;
  std::uint64_t atlas = 0;                  // the shared atlas's id
  std::uint32_t atlas_generation = 0;       // its generation now; 0 before it is baked
};

// Checks `list` (GraceUI's copy) as D76 says: counts, every command's ranges, every index against its command's own
// vertices, clip rectangles, texture ids and the atlas's generation. Never throws, reads nothing outside the copy.
Refusal check_draw_list(const DrawListData& list, const DrawContext& context) noexcept;

// One panel's lists: the writer (a mod's thread, holding writer_lock) fills back() and publishes it; the reader (the
// present thread, one at a time) takes the newest with read(). Neither ever waits for the other.
class DrawListBuffers {
 public:
  DrawListBuffers() = default;
  DrawListBuffers(const DrawListBuffers&) = delete;
  DrawListBuffers& operator=(const DrawListBuffers&) = delete;

  SpinLock& writer_lock() noexcept { return writer_; }
  DrawListData& back() noexcept { return buffers_[back_]; }  // under writer_lock()
  void publish() noexcept;                                    // under writer_lock(): back() becomes the newest

  // The reader's: the newest list published, or the one it already holds if none is newer. Its serial is 0 if no list
  // was ever published. The reader may change the list it holds (placed_x, placed_y and the vertices they move); it
  // stays the reader's until its next read().
  DrawListData& read() noexcept;
  std::uint64_t published() const noexcept { return published_.load(std::memory_order_relaxed); }

 private:
  static constexpr std::uint32_t kFresh = 4;

  SpinLock writer_;
  DrawListData buffers_[3];
  std::uint32_t back_ = 0;
  std::atomic<std::uint32_t> middle_{1};
  std::uint32_t front_ = 2;
  std::atomic<std::uint64_t> published_{0};
};

}  // namespace grace::abi
