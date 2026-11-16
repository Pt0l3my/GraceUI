// registry.h: the textures and faces mods register (D76, D80; invariant 11). Pure: the rules and the hand-overs between
// threads; graceui.dll's draw files hold the GPU objects, by slot.
//
// Textures. A mod registers a texture in its registration window, from its connect until its first submit, within its
// budget (GRACEUI_TEXTURES_PER_MOD textures and GRACEUI_TEXTURE_BYTES_PER_MOD bytes, EST) and the session's. Its pixels
// are copied on the mod's thread; the present thread uploads every texture waiting at its next frame once the renderer
// is warm (warm-up's own upload for the mods that came before it), without waiting for the copy: it draws a texture's
// commands only once its fence has passed. Nothing is freed on the present thread: a texture's pixels are freed on a
// mod's thread (sweep()) once the present thread no longer reads them, and its GPU objects, once its mod has gone, are
// released by the present thread after its last frame's fence, which frees nothing of GraceUI's own heap.
//
// Faces. A mod adds a face to the shared atlas before warm-up (at most GRACEUI_FACES_PER_MOD for a mod other than
// EldenNet, EST), within the atlas's room: each face's estimated area (face_area), counted when it is committed, so that
// the bake can never ask Dear ImGui for more than its largest texture (8192 by 8192) holds. Warm-up seals the table, and
// later faces are refused. A face's data stays for the session: the atlas reads it.
#pragma once

#include <atomic>
#include <cstdint>
#include <vector>

#include "grace/abi/consumers.h"
#include "grace/abi/spin.h"
#include "graceui.h"

namespace grace::abi {

inline constexpr std::uint32_t kMaxTextures = GRACEUI_TEXTURES_MAX;
inline constexpr std::uint32_t kTexturesPerMod = GRACEUI_TEXTURES_PER_MOD;
inline constexpr std::uint64_t kTextureBytesPerMod = GRACEUI_TEXTURE_BYTES_PER_MOD;
inline constexpr std::uint64_t kTextureBytesTotal = 64ull << 20;  // EST: every mod's together
inline constexpr std::uint32_t kTextureSideMax = GRACEUI_TEXTURE_SIDE_MAX;

inline constexpr std::uint32_t kMaxFaces = 24;  // EST: every mod's together
inline constexpr std::uint32_t kFacesPerMod = GRACEUI_FACES_PER_MOD;
inline constexpr std::uint32_t kFacesEldenNet = 8;  // EST: EldenNet's surfaces (D80)
inline constexpr std::uint64_t kFaceBytesMax = GRACEUI_FACE_BYTES_MAX;
inline constexpr std::uint32_t kFaceRangesMax = GRACEUI_FACE_RANGES_MAX;
inline constexpr std::uint32_t kFaceCodePointsMax = 65536;  // EST: code points a face's ranges cover in all
inline constexpr float kFacePixelsMin = 6.0f;
inline constexpr float kFacePixelsMax = 128.0f;
// The shared atlas's room for the mods' faces, in estimated pixels (face_area; D139 as amended on 2026-11-16, EST):
// every mod's together, and the share of the mods other than EldenNet (D80: EldenNet's surfaces come first). An
// estimate is an upper bound for most glyphs, so a full room bakes into well under Dear ImGui's 8192 by 8192 limit.
inline constexpr std::uint64_t kAtlasAreaMods = 8192ull * 4096;
inline constexpr std::uint64_t kAtlasAreaOthers = kAtlasAreaMods / 2;

// The shared atlas's texture id; a mod's textures have others (texture_id), never 0 and never this.
inline constexpr std::uint64_t kAtlasTexture = 1;
// Face ids: GraceUI's own from 1 (the notices' face is 1), the mods' from kFirstModFace, in registration order.
inline constexpr std::uint32_t kFirstModFace = 16;

constexpr std::uint64_t texture_id(std::uint32_t index, std::uint32_t generation) noexcept {
  return (static_cast<std::uint64_t>(generation) << 8) | (index + 2);
}

// A texture slot's state. A mod's thread moves it from free to pending, the present thread from pending to ready, and
// once its mod has gone (retire flag) from ready to released; a mod's thread then frees it.
enum class TextureState : std::uint32_t {
  free = 0,
  writing = 1,    // a mod's thread is copying its pixels in
  pending = 2,    // waiting for its upload
  claimed = 3,    // the present thread is copying its pixels into an upload buffer
  uploading = 4,  // its copy is queued on the GPU: its pixels are no longer read
  ready = 5,      // on the GPU: drawn
  released = 6,   // its mod has gone and its GPU objects are released: a mod's thread frees the slot
};

enum class Registered : std::uint8_t {
  ok = 0,
  invalid = 1,  // a description GraceUI does not take (format, sizes, a face's data or ranges)
  late = 2,     // after the mod's registration window (a texture) or after warm-up (a face)
  full = 3,     // over the mod's budget or the session's
};

// A texture as the present thread reads it while it holds the slot (claimed).
struct TexturePixels {
  std::uint32_t width = 0;
  std::uint32_t height = 0;
  const std::vector<unsigned char>* pixels = nullptr;  // width * height * 4 bytes, rows packed
};

// A face as warm-up reads it, once the table is sealed.
struct FaceData {
  std::uint32_t id = 0;
  float pixel_height = 0.0f;
  const std::vector<unsigned char>* data = nullptr;
  const std::vector<std::uint32_t>* ranges = nullptr;  // pairs of first and last, then 0 (Dear ImGui's form)
};

// A font file GraceUI hands to its rasteriser: an sfnt (TrueType or OpenType) whose table directory lies within it and
// names the tables a face needs.
bool valid_font(const unsigned char* data, std::uint64_t length) noexcept;

// A face's code-point ranges: 1 to kFaceRangesMax pairs, each first <= last, within 0x20 to 0x10FFFF and not into the
// surrogates, kFaceCodePointsMax in all.
bool valid_ranges(const std::uint32_t* pairs, std::uint64_t pair_count) noexcept;

// GraceUI's default ranges (§3.4): Latin, Latin-1, Latin Extended-A, General Punctuation and U+FFFD, as pairs then 0.
const std::uint32_t* default_ranges() noexcept;

// The code points of `ranges` (pairs then 0, Dear ImGui's form; null for default_ranges()) to which the font's character
// map gives a glyph: the subtable Dear ImGui's rasteriser (stb_truetype) reads, the last for platform 0 or for platform
// 3 with encoding 1 or 10, in its formats 0, 4, 6, 12 or 13. A code point a font lacks is not baked, so it takes no
// room. Every code point of the ranges if the map is missing, cut short or in another format (an upper bound).
std::uint64_t mapped_code_points(const unsigned char* data, std::uint64_t length, const std::uint32_t* ranges) noexcept;

// A face's estimated room in the shared atlas, in pixels: each of `glyphs` (and the fallback and ellipsis glyphs Dear
// ImGui adds) as a box of (2 * pixel_height + 2) by (pixel_height + 2), its bitmap oversampled twice across (D117) and
// padded; a glyph is seldom wider than the line is high, so this is an upper bound for most faces.
std::uint64_t face_area(std::uint64_t glyphs, float pixel_height) noexcept;

class Registry {
 public:
  Registry() = default;
  Registry(const Registry&) = delete;
  Registry& operator=(const Registry&) = delete;

  // ---- Textures, on mods' threads: begin (the checks and a slot), fill the slot's pixels, then commit or abort.
  Registered begin_texture(GraceUiConsumer owner, std::uint32_t width, std::uint32_t height,
                           std::uint32_t& slot) noexcept;
  std::vector<unsigned char>& texture_pixels(std::uint32_t slot) noexcept { return textures_[slot].pixels; }
  std::uint64_t commit_texture(std::uint32_t slot) noexcept;  // its id
  void abort_texture(std::uint32_t slot) noexcept;
  // The mod's first submit closes its registration window (later textures are late).
  void close_window(GraceUiConsumer owner) noexcept;
  // The mod's textures, for the checker: at most kTexturesPerMod ids into `ids`; answers how many.
  std::uint32_t owned(GraceUiConsumer owner, std::uint64_t* ids) const noexcept;
  // The mod has gone: its textures leave its budget, and the present thread releases those on the GPU.
  void retire_owner(GraceUiConsumer owner) noexcept;
  // Frees what the present thread no longer reads: the pixels of every texture uploaded, and every slot released.
  void sweep() noexcept;

  // ---- Textures, on the present thread.
  TextureState state(std::uint32_t slot) const noexcept {
    return textures_[slot].state.load(std::memory_order_acquire);
  }
  bool retiring(std::uint32_t slot) const noexcept { return textures_[slot].retire.load(std::memory_order_acquire); }
  bool claim(std::uint32_t slot, TexturePixels& out) noexcept;  // pending to claimed
  void uploading(std::uint32_t slot) noexcept;                  // claimed to uploading
  void ready(std::uint32_t slot) noexcept;                      // uploading to ready
  void released(std::uint32_t slot) noexcept;                   // its GPU objects released
  // The slot of a texture id that is ready to draw, or kMaxTextures.
  std::uint32_t drawable(std::uint64_t id) const noexcept;

  std::uint32_t textures() const noexcept;  // registered and not yet gone
  std::uint32_t textures_ready() const noexcept;

  // ---- Faces: a mod's thread begins, fills and commits; warm-up seals the table and reads what was committed.
  Registered begin_face(GraceUiConsumer owner, std::uint32_t budget, float pixel_height, std::uint32_t& slot) noexcept;
  std::vector<unsigned char>& face_data(std::uint32_t slot) noexcept { return faces_[slot].data; }
  std::vector<std::uint32_t>& face_ranges(std::uint32_t slot) noexcept { return faces_[slot].ranges; }
  // The face's room in the shared atlas, once its data is checked and before its commit: `area` (face_area) within
  // kAtlasAreaMods for every mod's faces together and, for a mod other than EldenNet, within kAtlasAreaOthers for theirs;
  // full past either. abort_face gives it back.
  Registered reserve_area(std::uint32_t slot, std::uint64_t area, bool eldennet) noexcept;
  std::uint64_t area_used() const noexcept;  // every committed or reserved face's
  // Its id, or 0 if warm-up sealed the table meanwhile (the face is then late, and abort_face frees it).
  std::uint32_t commit_face(std::uint32_t slot) noexcept;
  void abort_face(std::uint32_t slot) noexcept;
  // Warm-up (the present thread, once): no face is taken after this. Answers whether it sealed it now.
  bool seal_faces() noexcept;
  bool faces_sealed() const noexcept { return sealed_.load(std::memory_order_acquire); }
  // The `index`-th face committed before the seal, for warm-up; false past the last.
  bool face(std::uint32_t index, FaceData& out) const noexcept;
  std::uint32_t faces() const noexcept;  // committed
  // A face's owner, if it is a mod's face that was baked (0 otherwise).
  GraceUiConsumer face_owner(std::uint32_t face_id) const noexcept;

 private:
  struct OwnerBook {
    GraceUiConsumer owner = 0;  // the handle this entry counts for
    std::uint32_t textures = 0;
    std::uint64_t bytes = 0;
    std::uint32_t faces = 0;
    bool window_closed = false;
  };

  struct TextureSlot {
    std::atomic<TextureState> state{TextureState::free};
    std::atomic<bool> retire{false};
    std::atomic<std::uint32_t> generation{0};
    GraceUiConsumer owner = 0;  // written while writing, read by mods' threads under lock_
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::vector<unsigned char> pixels;
  };

  enum class FaceState : std::uint32_t { empty = 0, writing = 1, committed = 2, sealed = 3 };

  struct FaceSlot {
    std::atomic<FaceState> state{FaceState::empty};
    GraceUiConsumer owner = 0;
    float pixel_height = 0.0f;
    std::uint64_t area = 0;  // its reserved room in the atlas, under lock_
    bool others = false;     // counted in the share of the mods other than EldenNet
    std::vector<unsigned char> data;
    std::vector<std::uint32_t> ranges;
  };

  OwnerBook& book(GraceUiConsumer owner) noexcept;  // under lock_: the entry, reset if it counted for another handle

  mutable SpinLock lock_;  // mods' threads only
  OwnerBook books_[kMaxConsumers]{};
  std::uint64_t bytes_ = 0;  // every texture registered and not yet gone, under lock_
  std::uint64_t area_ = 0;         // every face's reserved room in the atlas, under lock_
  std::uint64_t others_area_ = 0;  // ... of the mods other than EldenNet
  TextureSlot textures_[kMaxTextures];
  FaceSlot faces_[kMaxFaces];
  std::atomic<bool> sealed_{false};
};

}  // namespace grace::abi
