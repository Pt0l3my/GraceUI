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
