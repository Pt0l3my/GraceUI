// The textures and faces mods register (registry.h): budgets and the registration window, a texture's life from a mod's
// copy to the GPU and back after its mod has gone, with nothing freed on the present thread; faces until warm-up seals
// the table, and the checks on a font file and its ranges.
#include <doctest/doctest.h>

#include <atomic>
#include <cstring>
#include <memory>
#include <thread>
#include <vector>

#include "alloc_counter.h"
#include "grace/abi/registry.h"

using namespace grace::abi;

namespace {

constexpr GraceUiConsumer kModA = (1ull << 8) | 1;
constexpr GraceUiConsumer kModAAgain = (2ull << 8) | 1;  // the same slot, connected again
constexpr GraceUiConsumer kModB = (1ull << 8) | 2;

std::uint64_t add_texture(Registry& registry, GraceUiConsumer owner, std::uint32_t width, std::uint32_t height,
                          Registered* result = nullptr) {
  std::uint32_t slot = 0;
  const Registered registered = registry.begin_texture(owner, width, height, slot);
  if (result != nullptr) {
    *result = registered;
  }
  if (registered != Registered::ok) {
    return 0;
  }
  registry.texture_pixels(slot).assign(std::size_t{width} * height * 4, 0x7F);
  return registry.commit_texture(slot);
}

// What the present thread does with every texture waiting: claim, copy, queue, and later see it ready.
std::uint32_t upload_all(Registry& registry) {
  std::uint32_t uploaded = 0;
  for (std::uint32_t i = 0; i < kMaxTextures; ++i) {
    TexturePixels pixels;
    if (registry.claim(i, pixels)) {
      REQUIRE(pixels.pixels->size() == std::size_t{pixels.width} * pixels.height * 4);
      registry.uploading(i);
      registry.ready(i);
      ++uploaded;
    }
  }
  return uploaded;
}

// A minimal sfnt header and table directory, as a font file starts: invented, and enough for the checks.
std::vector<unsigned char> font_like(std::uint32_t version = 0x00010000u, bool with_glyf = true) {
  const char* tags[] = {"cmap", "head", "hhea", "hmtx", "loca", "glyf"};
  const std::uint16_t count = with_glyf ? 6 : 5;
  std::vector<unsigned char> font(12 + 16 * count + 64, 0);
  const auto put32 = [&](std::size_t at, std::uint32_t value) {
    font[at] = static_cast<unsigned char>(value >> 24);
    font[at + 1] = static_cast<unsigned char>(value >> 16);
    font[at + 2] = static_cast<unsigned char>(value >> 8);
    font[at + 3] = static_cast<unsigned char>(value);
  };
  put32(0, version);
  font[4] = 0;
  font[5] = static_cast<unsigned char>(count);
  for (std::uint16_t i = 0; i < count; ++i) {
    const std::size_t record = 12 + 16u * i;
    std::memcpy(&font[record], tags[i], 4);
    put32(record + 8, static_cast<std::uint32_t>(12 + 16 * count + i * 8));
    put32(record + 12, 8);
  }
  return font;
}

}  // namespace

TEST_CASE("a texture's life: registered, uploaded by the present thread, drawable, and gone with its mod") {
  auto registry = std::make_unique<Registry>();
  const std::uint64_t id = add_texture(*registry, kModA, 64, 32);
  REQUIRE(id != 0);
  CHECK(id != kAtlasTexture);
  CHECK(registry->textures() == 1);
  CHECK(registry->drawable(id) == kMaxTextures);  // not on the GPU yet
  std::uint64_t ids[kTexturesPerMod];
  REQUIRE(registry->owned(kModA, ids) == 1);
  CHECK(ids[0] == id);
  CHECK(registry->owned(kModB, ids) == 0);

  CHECK(upload_all(*registry) == 1);
  CHECK(upload_all(*registry) == 0);  // uploaded once
  const std::uint32_t slot = registry->drawable(id);
  REQUIRE(slot < kMaxTextures);
  CHECK(registry->textures_ready() == 1);
  CHECK(registry->drawable(id + 256) == kMaxTextures);  // another generation of the same slot
  CHECK(registry->drawable(kAtlasTexture) == kMaxTextures);

  registry->sweep();  // the pixels go once the present thread no longer reads them: on a mod's thread
  registry->retire_owner(kModA);
  CHECK(registry->retiring(slot));
  CHECK(registry->drawable(id) == kMaxTextures);
  CHECK(registry->owned(kModA, ids) == 0);
  CHECK(registry->textures() == 0);
  // The present thread releases its GPU objects once its last frame's fence has passed; a mod's thread frees the slot.
  registry->released(slot);
  registry->sweep();
  CHECK(registry->state(slot) == TextureState::free);
}

TEST_CASE("a texture's budget: GRACEUI_TEXTURES_PER_MOD and GRACEUI_TEXTURE_BYTES_PER_MOD, per connection") {
  auto registry = std::make_unique<Registry>();
  Registered result = Registered::ok;
  for (std::uint32_t i = 0; i < kTexturesPerMod; ++i) {
    CHECK(add_texture(*registry, kModA, 8, 8) != 0);
  }
  CHECK(add_texture(*registry, kModA, 8, 8, &result) == 0);
  CHECK(result == Registered::full);
  CHECK(add_texture(*registry, kModB, 1024, 2048, &result) != 0);  // 8 MiB: the whole of B's budget
  CHECK(add_texture(*registry, kModB, 1, 1, &result) == 0);
  CHECK(result == Registered::full);
  CHECK(add_texture(*registry, kModB, 0, 8, &result) == 0);
  CHECK(result == Registered::invalid);
  CHECK(add_texture(*registry, kModB, kTextureSideMax + 1, 1, &result) == 0);
  CHECK(result == Registered::invalid);

  // A's budget is its connection's: once it has gone and connects again, it starts afresh.
  registry->retire_owner(kModA);
  CHECK(add_texture(*registry, kModAAgain, 8, 8, &result) != 0);
}

TEST_CASE("the registration window closes at the mod's first submit") {
  auto registry = std::make_unique<Registry>();
  CHECK(add_texture(*registry, kModA, 4, 4) != 0);
  registry->close_window(kModA);
  Registered result = Registered::ok;
  CHECK(add_texture(*registry, kModA, 4, 4, &result) == 0);
  CHECK(result == Registered::late);
  CHECK(add_texture(*registry, kModB, 4, 4, &result) != 0);  // another mod's window is its own
  registry->retire_owner(kModA);
  CHECK(add_texture(*registry, kModAAgain, 4, 4, &result) != 0);  // a new connection, a new window
}

TEST_CASE("a texture never uploaded, whose mod has gone, is taken back on a mod's thread; an aborted one too") {
  auto registry = std::make_unique<Registry>();
  const std::uint64_t id = add_texture(*registry, kModA, 16, 16);
  REQUIRE(id != 0);
  registry->retire_owner(kModA);
  CHECK(upload_all(*registry) == 0);  // the present thread skips it
  registry->sweep();
  for (std::uint32_t i = 0; i < kMaxTextures; ++i) {
    CHECK(registry->state(i) == TextureState::free);
  }
  std::uint32_t slot = 0;
  REQUIRE(registry->begin_texture(kModB, 8, 8, slot) == Registered::ok);
  registry->abort_texture(slot);
  CHECK(registry->state(slot) == TextureState::free);
  CHECK(registry->textures() == 0);
}

TEST_CASE("the present thread's side of textures allocates and frees nothing, while a mod's thread registers") {
  auto registry = std::make_unique<Registry>();
  std::atomic<bool> stop{false};
  std::atomic<std::uint32_t> added{0};
  std::thread mod([&] {
    for (std::uint32_t n = 0; !stop.load(std::memory_order_relaxed); ++n) {
      const GraceUiConsumer owner = (static_cast<GraceUiConsumer>(n + 1) << 8) | 1;
      add_texture(*registry, owner, 16 + n % 16, 8);
      added.fetch_add(1, std::memory_order_relaxed);
      std::this_thread::yield();  // the present thread's chance to upload before the mod goes
      add_texture(*registry, owner, 8, 8);
      registry->retire_owner(owner);  // gone again: whatever was uploaded must be released by the present thread
    }
  });
  test::Counts counts;
  std::uint64_t uploads = 0;
  {
    const test::Counting counting;
    while (added.load(std::memory_order_relaxed) == 0) {
      std::this_thread::yield();
    }
    for (int round = 0; round < 3000 || (uploads == 0 && round < 3000000); ++round) {
      for (std::uint32_t i = 0; i < kMaxTextures; ++i) {
        TexturePixels pixels;
        if (registry->claim(i, pixels)) {
          volatile unsigned char first = (*pixels.pixels)[0];  // the copy into an upload buffer
          (void)first;
          registry->uploading(i);
          registry->ready(i);
          ++uploads;
        } else if (registry->retiring(i) &&
                   (registry->state(i) == TextureState::ready || registry->state(i) == TextureState::uploading)) {
          registry->released(i);
        }
      }
    }
    counts = counting.counts();
  }
  stop.store(true);
  mod.join();
  CHECK(uploads > 0);
  CHECK(counts.allocations == 0);
  CHECK(counts.frees == 0);
}

TEST_CASE("faces: a budget per mod, the table sealed at warm-up, a face being written then late") {
  auto registry = std::make_unique<Registry>();
  std::uint32_t slot = 0;
  CHECK(registry->begin_face(kModA, kFacesPerMod, 3.0f, slot) == Registered::invalid);
  CHECK(registry->begin_face(kModA, kFacesPerMod, 200.0f, slot) == Registered::invalid);
  REQUIRE(registry->begin_face(kModA, kFacesPerMod, 15.0f, slot) == Registered::ok);
  registry->face_data(slot) = font_like();
  const std::uint32_t first = registry->commit_face(slot);
  CHECK(first == kFirstModFace + slot);
  REQUIRE(registry->begin_face(kModA, kFacesPerMod, 20.0f, slot) == Registered::ok);
  CHECK(registry->commit_face(slot) != 0);
  CHECK(registry->begin_face(kModA, kFacesPerMod, 20.0f, slot) == Registered::full);  // two for a mod (EST)
  for (std::uint32_t i = 0; i < kFacesEldenNet; ++i) {  // EldenNet's surfaces have more (D80)
    REQUIRE(registry->begin_face(kModB, kFacesEldenNet, 13.0f, slot) == Registered::ok);
    CHECK(registry->commit_face(slot) != 0);
  }
  CHECK(registry->begin_face(kModB, kFacesEldenNet, 13.0f, slot) == Registered::full);
  CHECK(registry->face_owner(first) == kModA);

  // A face being written while warm-up seals the table is late: its commit fails, and its writer frees it.
  const GraceUiConsumer third = (1ull << 8) | 3;
  REQUIRE(registry->begin_face(third, kFacesPerMod, 15.0f, slot) == Registered::ok);
  CHECK(registry->seal_faces());
  CHECK_FALSE(registry->seal_faces());  // once
  CHECK(registry->commit_face(slot) == 0);
  registry->abort_face(slot);
  CHECK(registry->begin_face(third, kFacesPerMod, 15.0f, slot) == Registered::late);
  CHECK(registry->faces() == 2 + kFacesEldenNet);
  FaceData face;
  REQUIRE(registry->face(0, face));
  CHECK(face.id == first);
  CHECK(face.pixel_height == 15.0f);
  CHECK(face.data->size() == font_like().size());
  CHECK_FALSE(registry->face(2 + kFacesEldenNet, face));
  CHECK(registry->face_owner(face.id + 100) == 0);
}

TEST_CASE("a font file: an sfnt whose table directory lies within it and names a face's tables") {
  std::vector<unsigned char> font = font_like();
  CHECK(valid_font(font.data(), font.size()));
  CHECK(valid_font(font_like(0x4F54544Fu).data(), font.size()));  // 'OTTO', with the TrueType outlines here too
  CHECK_FALSE(valid_font(font_like(0x74746366u).data(), font.size()));  // 'ttcf': a collection
  CHECK_FALSE(valid_font(font_like(0x12345678u).data(), font.size()));
  CHECK_FALSE(valid_font(font_like(0x00010000u, false).data(), font_like(0x00010000u, false).size()));  // no glyf
  CHECK_FALSE(valid_font(font.data(), 40));  // cut short: the directory runs past the end
  CHECK_FALSE(valid_font(nullptr, 0));
  font[12 + 16 * 5 + 11] = 0xFF;  // the last table's offset far past the end
  CHECK_FALSE(valid_font(font.data(), font.size()));
}

TEST_CASE("a face's ranges: pairs within 0x20 to 0x10FFFF, outside the surrogates, bounded") {
  const std::uint32_t good[] = {0x20, 0x7E, 0xA0, 0xFF};
  CHECK(valid_ranges(good, 2));
  CHECK_FALSE(valid_ranges(good, 0));
  CHECK_FALSE(valid_ranges(nullptr, 1));
  const std::uint32_t inverted[] = {0x7E, 0x20};
  CHECK_FALSE(valid_ranges(inverted, 1));
  const std::uint32_t control[] = {0x00, 0x7E};
  CHECK_FALSE(valid_ranges(control, 1));
  const std::uint32_t surrogates[] = {0xD000, 0xD900};
  CHECK_FALSE(valid_ranges(surrogates, 1));
  const std::uint32_t beyond[] = {0x10FFFF, 0x110000};
  CHECK_FALSE(valid_ranges(beyond, 1));
  const std::uint32_t too_many_points[] = {0x20, 0x20 + kFaceCodePointsMax};
  CHECK_FALSE(valid_ranges(too_many_points, 1));
  std::vector<std::uint32_t> many(2 * (kFaceRangesMax + 1), 0x41);
  CHECK_FALSE(valid_ranges(many.data(), kFaceRangesMax + 1));
  CHECK(valid_ranges(many.data(), kFaceRangesMax));
  const std::uint32_t* defaults = default_ranges();
  std::uint64_t pairs = 0;
  while (defaults[2 * pairs] != 0) {
    ++pairs;
  }
  CHECK(valid_ranges(defaults, pairs));
}
