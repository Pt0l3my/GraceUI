// registry.cpp: the textures and faces mods register (registry.h).
#include "grace/abi/registry.h"

#include <cstring>

namespace grace::abi {
namespace {

std::uint32_t read_u32(const unsigned char* at) noexcept {
  return (std::uint32_t{at[0]} << 24) | (std::uint32_t{at[1]} << 16) | (std::uint32_t{at[2]} << 8) | at[3];
}

std::uint16_t read_u16(const unsigned char* at) noexcept {
  return static_cast<std::uint16_t>((at[0] << 8) | at[1]);
}

constexpr std::uint32_t tag(const char (&name)[5]) noexcept {
  return (std::uint32_t{static_cast<unsigned char>(name[0])} << 24) |
         (std::uint32_t{static_cast<unsigned char>(name[1])} << 16) |
         (std::uint32_t{static_cast<unsigned char>(name[2])} << 8) | static_cast<unsigned char>(name[3]);
}

constexpr std::uint32_t kDefaultRanges[] = {
    0x0020, 0x007E,  // Basic Latin
    0x00A0, 0x00FF,  // Latin-1 Supplement
    0x0100, 0x017F,  // Latin Extended-A
    0x2000, 0x206F,  // General Punctuation
    0xFFFD, 0xFFFD,  // the replacement character
    0,
};

void free_vector(std::vector<unsigned char>& bytes) noexcept { std::vector<unsigned char>().swap(bytes); }

}  // namespace

bool valid_font(const unsigned char* data, std::uint64_t length) noexcept {
  if (data == nullptr || length < 12 || length > kFaceBytesMax) {
    return false;
  }
  const std::uint32_t version = read_u32(data);
  if (version != 0x00010000u && version != tag("OTTO") && version != tag("true")) {
    return false;  // a collection or anything else: one face a file
  }
  const std::uint32_t tables = read_u16(data + 4);
  if (tables == 0 || 12 + std::uint64_t{tables} * 16 > length) {
    return false;
  }
  bool cmap = false, head = false, hhea = false, hmtx = false, outlines = false, loca = false, glyf = false;
  for (std::uint32_t i = 0; i < tables; ++i) {
    const unsigned char* record = data + 12 + std::size_t{i} * 16;
    const std::uint32_t name = read_u32(record);
    const std::uint64_t offset = read_u32(record + 8);
    const std::uint64_t size = read_u32(record + 12);
    if (offset + size > length) {
      return false;
    }
    cmap = cmap || name == tag("cmap");
    head = head || name == tag("head");
    hhea = hhea || name == tag("hhea");
    hmtx = hmtx || name == tag("hmtx");
    loca = loca || name == tag("loca");
    glyf = glyf || name == tag("glyf");
    outlines = outlines || name == tag("CFF ");
  }
  return cmap && head && hhea && hmtx && (outlines || (loca && glyf));
}

bool valid_ranges(const std::uint32_t* pairs, std::uint64_t pair_count) noexcept {
  if (pairs == nullptr || pair_count == 0 || pair_count > kFaceRangesMax) {
    return false;
  }
  std::uint64_t points = 0;
  for (std::uint64_t i = 0; i < pair_count; ++i) {
    const std::uint32_t first = pairs[2 * i];
    const std::uint32_t last = pairs[2 * i + 1];
    if (first < 0x20 || first > last || last > 0x10FFFF || (first <= 0xDFFF && last >= 0xD800)) {
      return false;
    }
    points += std::uint64_t{last} - first + 1;
  }
  return points <= kFaceCodePointsMax;
}

const std::uint32_t* default_ranges() noexcept { return kDefaultRanges; }

namespace {

// A font's bytes, read only within their length.
struct Bytes {
  const unsigned char* data;
  std::uint64_t length;

  bool has(std::uint64_t offset, std::uint64_t size) const noexcept {
    return offset <= length && size <= length - offset;
  }
  std::uint32_t u16(std::uint64_t offset) const noexcept { return read_u16(data + offset); }
  std::uint32_t u32(std::uint64_t offset) const noexcept { return read_u32(data + offset); }
};

// Where the character map's subtable that stb_truetype reads begins (stbtt_InitFont's choice: the last record for
// platform 0, or for platform 3 with encoding 1 or 10), or false if there is none within the file.
bool unicode_subtable(const Bytes& font, std::uint64_t& at) noexcept {
  if (!font.has(0, 12)) {
    return false;
  }
  const std::uint32_t tables = font.u16(4);
  std::uint64_t cmap = 0;
  for (std::uint32_t i = 0; i < tables && cmap == 0; ++i) {
    const std::uint64_t record = 12 + std::uint64_t{i} * 16;
    if (font.has(record, 16) && font.u32(record) == tag("cmap")) {
      cmap = font.u32(record + 8);
    }
  }
  if (cmap == 0 || !font.has(cmap, 4)) {
    return false;
  }
  const std::uint32_t records = font.u16(cmap + 2);
  bool found = false;
  for (std::uint32_t i = 0; i < records; ++i) {
    const std::uint64_t record = cmap + 4 + std::uint64_t{i} * 8;
    if (!font.has(record, 8)) {
      return false;
    }
    const std::uint32_t platform = font.u16(record);
    const std::uint32_t encoding = font.u16(record + 2);
    if (platform == 0 || (platform == 3 && (encoding == 1 || encoding == 10))) {
      at = cmap + font.u32(record + 4);
      found = true;
    }
  }
  return found && font.has(at, 2);
}

// The code points in [first, last] that one span of a map gives a glyph, its glyph for a code point answered by `glyph`
// (0: none); spans that cannot be glyphless but at one point are counted whole by their callers.
template <class Glyph>
std::uint64_t count_each(std::uint32_t first, std::uint32_t last, Glyph glyph) noexcept {
  std::uint64_t count = 0;
  for (std::uint64_t c = first; c <= last; ++c) {
    count += glyph(static_cast<std::uint32_t>(c)) != 0 ? 1u : 0u;
  }
  return count;
}

// The code points of one range [first, last] the subtable at `at` maps to a glyph; false if it cannot be read.
bool mapped_in(const Bytes& font, std::uint64_t at, std::uint32_t first, std::uint32_t last,
               std::uint64_t& count) noexcept {
  const std::uint32_t format = font.u16(at);
  const auto overlap = [&](std::uint32_t start, std::uint32_t end, std::uint32_t& lo, std::uint32_t& hi) {
    lo = first > start ? first : start;
    hi = last < end ? last : end;
    return lo <= hi;
  };
  std::uint32_t lo = 0;
  std::uint32_t hi = 0;
  switch (format) {
    case 0: {  // 256 one-byte glyph ids
      if (!font.has(at, 6 + 256)) {
        return false;
      }
      if (overlap(0, 255, lo, hi)) {
        count += count_each(lo, hi, [&](std::uint32_t c) { return std::uint32_t{font.data[at + 6 + c]}; });
      }
      return true;
    }
    case 6: {  // one run of two-byte glyph ids
      if (!font.has(at, 10)) {
        return false;
      }
      const std::uint32_t start = font.u16(at + 6);
      const std::uint32_t entries = font.u16(at + 8);
      if (entries == 0 || !font.has(at + 10, std::uint64_t{entries} * 2)) {
        return entries == 0;
      }
      if (overlap(start, start + entries - 1, lo, hi)) {
        count += count_each(lo, hi, [&](std::uint32_t c) { return font.u16(at + 10 + std::uint64_t{c - start} * 2); });
      }
      return true;
    }
    case 4: {  // segments of the BMP
      if (!font.has(at, 14)) {
        return false;
      }
      const std::uint32_t segments = font.u16(at + 6) / 2;
      const std::uint64_t ends = at + 14;
      const std::uint64_t starts = ends + std::uint64_t{segments} * 2 + 2;
      const std::uint64_t deltas = starts + std::uint64_t{segments} * 2;
      const std::uint64_t offsets = deltas + std::uint64_t{segments} * 2;
      if (!font.has(ends, std::uint64_t{segments} * 8 + 2)) {
        return false;
      }
      for (std::uint32_t i = 0; i < segments; ++i) {
        const std::uint32_t start = font.u16(starts + i * 2);
        const std::uint32_t end = font.u16(ends + i * 2);
        if (start > end || !overlap(start, end, lo, hi)) {
          continue;
        }
        const std::uint32_t delta = font.u16(deltas + i * 2);
        const std::uint32_t offset = font.u16(offsets + i * 2);
        if (offset == 0) {
          // glyph = (c + delta) mod 65536, which is 0 for one code point at most
          const std::uint32_t none = (0x10000u - delta) & 0xFFFFu;
          count += std::uint64_t{hi} - lo + 1 - (none >= lo && none <= hi ? 1u : 0u);
          continue;
        }
        count += count_each(lo, hi, [&](std::uint32_t c) -> std::uint32_t {
          const std::uint64_t where = offsets + i * 2 + offset + std::uint64_t{c - start} * 2;
          if (!font.has(where, 2)) {
            return 0;
          }
          const std::uint32_t id = font.u16(where);
          return id == 0 ? 0 : (id + delta) & 0xFFFFu;
        });
      }
      return true;
    }
    case 12:    // groups of consecutive glyphs
    case 13: {  // groups sharing one glyph
      if (!font.has(at, 16)) {
        return false;
      }
      const std::uint64_t groups = font.u32(at + 12);
      if (!font.has(at + 16, groups * 12)) {
        return false;
      }
      for (std::uint64_t g = 0; g < groups; ++g) {
        const std::uint64_t group = at + 16 + g * 12;
        const std::uint32_t start = font.u32(group);
        const std::uint32_t end = font.u32(group + 4);
        const std::uint32_t glyph = font.u32(group + 8);
        if (start > end || !overlap(start, end, lo, hi)) {
          continue;
        }
        std::uint64_t mapped = std::uint64_t{hi} - lo + 1;
        if (glyph == 0 && (format == 13 || lo == start)) {
          mapped = format == 13 ? 0 : mapped - 1;  // glyph 0 is "no glyph"
        }
        count += mapped;
      }
      return true;
    }
    default: return false;
  }
}

}  // namespace

std::uint64_t mapped_code_points(const unsigned char* data, std::uint64_t length, const std::uint32_t* ranges) noexcept {
  if (ranges == nullptr) {
    ranges = kDefaultRanges;
  }
  std::uint64_t points = 0;
  for (const std::uint32_t* pair = ranges; pair[0] != 0; pair += 2) {
    points += pair[1] >= pair[0] ? std::uint64_t{pair[1]} - pair[0] + 1 : 0;
  }
  const Bytes font{data, data != nullptr ? length : 0};
  std::uint64_t at = 0;
  if (!unicode_subtable(font, at)) {
    return points;
  }
  std::uint64_t mapped = 0;
  for (const std::uint32_t* pair = ranges; pair[0] != 0; pair += 2) {
    if (pair[1] >= pair[0] && !mapped_in(font, at, pair[0], pair[1], mapped)) {
      return points;
    }
  }
  return mapped < points ? mapped : points;
}

std::uint64_t face_area(std::uint64_t glyphs, float pixel_height) noexcept {
  const auto pixels = static_cast<std::uint64_t>(pixel_height > 0.0f ? pixel_height + 0.999f : 0.0f);
  return (glyphs + 2) * (2 * pixels + 2) * (pixels + 2);
}

Registry::OwnerBook& Registry::book(GraceUiConsumer owner) noexcept {
  OwnerBook& entry = books_[(owner & 0xFFu) % kMaxConsumers];
  if (entry.owner != owner) {
    entry = OwnerBook{};
    entry.owner = owner;
  }
  return entry;
}

// ---- Textures.

Registered Registry::begin_texture(GraceUiConsumer owner, std::uint32_t width, std::uint32_t height,
                                   std::uint32_t& slot) noexcept {
  if (owner == 0 || width == 0 || height == 0 || width > kTextureSideMax || height > kTextureSideMax) {
    return Registered::invalid;
  }
  const std::uint64_t bytes = std::uint64_t{width} * height * 4;
  const SpinGuard guard(lock_);
  OwnerBook& entry = book(owner);
  if (entry.window_closed) {
    return Registered::late;
  }
  if (entry.textures >= kTexturesPerMod || entry.bytes + bytes > kTextureBytesPerMod ||
      bytes_ + bytes > kTextureBytesTotal) {
    return Registered::full;
  }
  for (std::uint32_t i = 0; i < kMaxTextures; ++i) {
    TextureState expected = TextureState::free;
    if (textures_[i].state.compare_exchange_strong(expected, TextureState::writing, std::memory_order_acq_rel)) {
      TextureSlot& texture = textures_[i];
      texture.owner = owner;
      texture.width = width;
      texture.height = height;
      texture.retire.store(false, std::memory_order_relaxed);
      ++entry.textures;
      entry.bytes += bytes;
      bytes_ += bytes;
      slot = i;
      return Registered::ok;
    }
  }
  return Registered::full;
}

std::uint64_t Registry::commit_texture(std::uint32_t slot) noexcept {
  TextureSlot& texture = textures_[slot];
  const std::uint32_t generation = texture.generation.load(std::memory_order_relaxed) + 1;
  texture.generation.store(generation, std::memory_order_relaxed);
  texture.state.store(TextureState::pending, std::memory_order_release);
  return texture_id(slot, generation);
}

void Registry::abort_texture(std::uint32_t slot) noexcept {
  const SpinGuard guard(lock_);
  TextureSlot& texture = textures_[slot];
  const std::uint64_t bytes = std::uint64_t{texture.width} * texture.height * 4;
  OwnerBook& entry = book(texture.owner);
  entry.textures -= entry.textures > 0 ? 1 : 0;
  entry.bytes -= entry.bytes >= bytes ? bytes : entry.bytes;
  bytes_ -= bytes_ >= bytes ? bytes : bytes_;
  free_vector(texture.pixels);
  texture.owner = 0;
  texture.state.store(TextureState::free, std::memory_order_release);
}

void Registry::close_window(GraceUiConsumer owner) noexcept {
  const SpinGuard guard(lock_);
  book(owner).window_closed = true;
}

std::uint32_t Registry::owned(GraceUiConsumer owner, std::uint64_t* ids) const noexcept {
  const SpinGuard guard(lock_);
  std::uint32_t count = 0;
  for (std::uint32_t i = 0; i < kMaxTextures && count < kTexturesPerMod; ++i) {
    const TextureSlot& texture = textures_[i];
    const TextureState state = texture.state.load(std::memory_order_acquire);
    if (texture.owner == owner && state >= TextureState::pending && state <= TextureState::ready &&
        !texture.retire.load(std::memory_order_relaxed)) {
      ids[count++] = texture_id(i, texture.generation.load(std::memory_order_relaxed));
    }
  }
  return count;
}

void Registry::retire_owner(GraceUiConsumer owner) noexcept {
  {
    const SpinGuard guard(lock_);
    for (std::uint32_t i = 0; i < kMaxTextures; ++i) {
      TextureSlot& texture = textures_[i];
      const TextureState state = texture.state.load(std::memory_order_acquire);
      if (texture.owner != owner || state == TextureState::free || state == TextureState::released) {
        continue;
      }
      texture.retire.store(true, std::memory_order_release);
      const std::uint64_t bytes = std::uint64_t{texture.width} * texture.height * 4;
      bytes_ -= bytes_ >= bytes ? bytes : bytes_;
    }
    OwnerBook& entry = books_[(owner & 0xFFu) % kMaxConsumers];
    if (entry.owner == owner) {
      entry = OwnerBook{};
    }
  }
  sweep();
}

void Registry::sweep() noexcept {
  const SpinGuard guard(lock_);
  for (TextureSlot& texture : textures_) {
    TextureState state = texture.state.load(std::memory_order_acquire);
    if (state == TextureState::pending && texture.retire.load(std::memory_order_acquire)) {
      // Never uploaded, and its mod has gone: the present thread skips it, so it is taken back here.
      if (texture.state.compare_exchange_strong(state, TextureState::writing, std::memory_order_acq_rel)) {
        free_vector(texture.pixels);
        texture.owner = 0;
        texture.retire.store(false, std::memory_order_relaxed);
        texture.state.store(TextureState::free, std::memory_order_release);
      }
      continue;
    }
    if (state >= TextureState::uploading && texture.pixels.capacity() != 0) {
      free_vector(texture.pixels);  // the present thread copied them before it moved the slot to uploading
    }
    if (state == TextureState::released) {
      texture.owner = 0;
      texture.retire.store(false, std::memory_order_relaxed);
      texture.state.store(TextureState::free, std::memory_order_release);
    }
  }
}

bool Registry::claim(std::uint32_t slot, TexturePixels& out) noexcept {
  TextureSlot& texture = textures_[slot];
  if (texture.retire.load(std::memory_order_acquire)) {
    return false;
  }
  TextureState expected = TextureState::pending;
  if (!texture.state.compare_exchange_strong(expected, TextureState::claimed, std::memory_order_acq_rel)) {
    return false;
  }
  out.width = texture.width;
  out.height = texture.height;
  out.pixels = &texture.pixels;
  return true;
}

void Registry::uploading(std::uint32_t slot) noexcept {
  textures_[slot].state.store(TextureState::uploading, std::memory_order_release);
}

void Registry::ready(std::uint32_t slot) noexcept {
  TextureState expected = TextureState::uploading;
  textures_[slot].state.compare_exchange_strong(expected, TextureState::ready, std::memory_order_acq_rel);
}

void Registry::released(std::uint32_t slot) noexcept {
  textures_[slot].state.store(TextureState::released, std::memory_order_release);
}

std::uint32_t Registry::drawable(std::uint64_t id) const noexcept {
  const std::uint64_t low = id & 0xFFu;
  if (low < 2 || low - 2 >= kMaxTextures) {
    return kMaxTextures;
  }
  const std::uint32_t slot = static_cast<std::uint32_t>(low - 2);
  const TextureSlot& texture = textures_[slot];
  if (texture.state.load(std::memory_order_acquire) != TextureState::ready ||
      texture.retire.load(std::memory_order_acquire) ||
      (id >> 8) != texture.generation.load(std::memory_order_relaxed)) {
    return kMaxTextures;
  }
  return slot;
}

std::uint32_t Registry::textures() const noexcept {
  std::uint32_t count = 0;
  for (const TextureSlot& texture : textures_) {
    const TextureState state = texture.state.load(std::memory_order_acquire);
    count += state >= TextureState::pending && state <= TextureState::ready &&
                     !texture.retire.load(std::memory_order_relaxed)
                 ? 1u
                 : 0u;
  }
  return count;
}

std::uint32_t Registry::textures_ready() const noexcept {
  std::uint32_t count = 0;
  for (const TextureSlot& texture : textures_) {
    count += texture.state.load(std::memory_order_acquire) == TextureState::ready ? 1u : 0u;
  }
  return count;
}

// ---- Faces.

Registered Registry::begin_face(GraceUiConsumer owner, std::uint32_t budget, float pixel_height,
                                std::uint32_t& slot) noexcept {
  if (owner == 0 || !(pixel_height >= kFacePixelsMin && pixel_height <= kFacePixelsMax)) {
    return Registered::invalid;
  }
  const SpinGuard guard(lock_);
  if (sealed_.load(std::memory_order_acquire)) {
    return Registered::late;
  }
  OwnerBook& entry = book(owner);
  if (entry.faces >= budget) {
    return Registered::full;
  }
  for (std::uint32_t i = 0; i < kMaxFaces; ++i) {
    FaceState expected = FaceState::empty;
    if (faces_[i].state.compare_exchange_strong(expected, FaceState::writing, std::memory_order_acq_rel)) {
      faces_[i].owner = owner;
      faces_[i].pixel_height = pixel_height;
      ++entry.faces;
      slot = i;
      return Registered::ok;
    }
    if (expected == FaceState::sealed) {
      return Registered::late;  // warm-up is sealing the table now
    }
  }
  return Registered::full;
}

Registered Registry::reserve_area(std::uint32_t slot, std::uint64_t area, bool eldennet) noexcept {
  const SpinGuard guard(lock_);
  FaceSlot& face = faces_[slot];
  if (area > kAtlasAreaMods - area_ || (!eldennet && area > kAtlasAreaOthers - others_area_)) {
    return Registered::full;
  }
  area_ += area;
  others_area_ += eldennet ? 0 : area;
  face.area = area;
  face.others = !eldennet;
  return Registered::ok;
}

std::uint64_t Registry::area_used() const noexcept {
  const SpinGuard guard(lock_);
  return area_;
}

std::uint32_t Registry::commit_face(std::uint32_t slot) noexcept {
  FaceState expected = FaceState::writing;
  if (!faces_[slot].state.compare_exchange_strong(expected, FaceState::committed, std::memory_order_acq_rel)) {
    return 0;  // sealed while it was written
  }
  return kFirstModFace + slot;
}

void Registry::abort_face(std::uint32_t slot) noexcept {
  const SpinGuard guard(lock_);
  FaceSlot& face = faces_[slot];
  OwnerBook& entry = book(face.owner);
  entry.faces -= entry.faces > 0 ? 1 : 0;
  area_ -= face.area;
  others_area_ -= face.others ? face.area : 0;
  face.area = 0;
  face.others = false;
  free_vector(face.data);
  std::vector<std::uint32_t>().swap(face.ranges);
  face.owner = 0;
  FaceState expected = FaceState::writing;
  face.state.compare_exchange_strong(expected, FaceState::empty, std::memory_order_acq_rel);  // a sealed one stays so
}

bool Registry::seal_faces() noexcept {
  if (sealed_.exchange(true, std::memory_order_acq_rel)) {
    return false;
  }
  for (FaceSlot& face : faces_) {
    FaceState expected = FaceState::empty;
    if (!face.state.compare_exchange_strong(expected, FaceState::sealed, std::memory_order_acq_rel) &&
        expected == FaceState::writing) {
      // A face still being written is late: its commit now fails, and its writer frees it.
      face.state.compare_exchange_strong(expected, FaceState::sealed, std::memory_order_acq_rel);
    }
  }
  return true;
}

bool Registry::face(std::uint32_t index, FaceData& out) const noexcept {
  for (std::uint32_t i = 0; i < kMaxFaces; ++i) {
    const FaceSlot& face = faces_[i];
    if (face.state.load(std::memory_order_acquire) == FaceState::committed && index-- == 0) {
      out.id = kFirstModFace + i;
      out.pixel_height = face.pixel_height;
      out.data = &face.data;
      out.ranges = &face.ranges;
      return true;
    }
  }
  return false;
}

std::uint32_t Registry::faces() const noexcept {
  std::uint32_t count = 0;
  for (const FaceSlot& face : faces_) {
    count += face.state.load(std::memory_order_acquire) == FaceState::committed ? 1u : 0u;
  }
  return count;
}

GraceUiConsumer Registry::face_owner(std::uint32_t face_id) const noexcept {
  if (face_id < kFirstModFace || face_id - kFirstModFace >= kMaxFaces) {
    return 0;
  }
  const FaceSlot& face = faces_[face_id - kFirstModFace];
  return face.state.load(std::memory_order_acquire) == FaceState::committed ? face.owner : 0;
}

}  // namespace grace::abi
