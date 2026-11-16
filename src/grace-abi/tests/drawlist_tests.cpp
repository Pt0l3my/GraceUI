// A panel's draw lists (drawlist.h): the checker's every refusal and what it lets through, and the triple buffer the
// present thread reads, which never hands it a list half written and never allocates or frees on its side.
#include <doctest/doctest.h>

#include <atomic>
#include <cmath>
#include <limits>
#include <memory>
#include <thread>

#include "alloc_counter.h"
#include "grace/abi/drawlist.h"

using namespace grace::abi;

namespace {

constexpr std::uint64_t kOwnTexture = 0x1234'0002;
constexpr std::uint64_t kOtherTexture = 0x1234'0003;
constexpr std::uint64_t kAtlas = 1;

DrawContext context_for(const std::uint64_t* textures, std::uint32_t count, std::uint32_t generation = 1) {
  DrawContext context;
  context.width = 200.0f;
  context.height = 100.0f;
  context.textures = textures;
  context.texture_count = count;
  context.atlas = kAtlas;
  context.atlas_generation = generation;
  return context;
}

GraceUiDrawCommand command(std::uint32_t vertex_offset, std::uint32_t vertex_count, std::uint32_t index_offset,
                           std::uint32_t index_count, std::uint64_t texture = kOwnTexture) {
  GraceUiDrawCommand c{};
  c.clip_x0 = 0.0f;
  c.clip_y0 = 0.0f;
  c.clip_x1 = 200.0f;
  c.clip_y1 = 100.0f;
  c.texture = texture;
  c.vertex_offset = vertex_offset;
  c.vertex_count = vertex_count;
  c.index_offset = index_offset;
  c.index_count = index_count;
  return c;
}

// Two quads, each a command of its own over its own four vertices: the second's indices are relative to its offset.
DrawListData two_quads() {
  DrawListData list;
  list.vertices.resize(8);
  for (std::uint32_t i = 0; i < 8; ++i) {
    list.vertices[i] = GraceUiDrawVert{static_cast<float>(i * 10), 5.0f, 0.0f, 0.0f, 0xFFFFFFFFu};
  }
  list.indices = {0, 1, 2, 0, 2, 3, 0, 1, 2, 0, 2, 3};
  list.commands = {command(0, 4, 0, 6), command(4, 4, 6, 6)};
  return list;
}

}  // namespace

TEST_CASE("a well-formed list passes, with each command's indices relative to its own vertices") {
  const std::uint64_t textures[] = {kOwnTexture};
  CHECK(check_draw_list(two_quads(), context_for(textures, 1)) == Refusal::none);
  CHECK(check_draw_list(DrawListData{}, context_for(textures, 1)) == Refusal::none);  // empty: draws nothing

  DrawListData edges = two_quads();
  edges.commands[0].clip_x0 = 0.0f;
  edges.commands[0].clip_x1 = 200.0f;  // the panel's very edges
  edges.commands[1].clip_x0 = 50.0f;
  edges.commands[1].clip_x1 = 50.0f;  // an empty clip rectangle draws nothing, and is no fault
  edges.commands[1].index_count = 0;
  CHECK(check_draw_list(edges, context_for(textures, 1)) == Refusal::none);
}

TEST_CASE("the size bounds: vertices, indices and commands") {
  const std::uint64_t textures[] = {kOwnTexture};
  DrawListData list;
  list.vertices.resize(kDrawVerticesMax);
  CHECK(check_draw_list(list, context_for(textures, 1)) == Refusal::none);
  list.vertices.resize(kDrawVerticesMax + 1);
  CHECK(check_draw_list(list, context_for(textures, 1)) == Refusal::too_large);
  CHECK(kDrawVerticesMax * sizeof(GraceUiDrawVert) <= 256u * 1024u);

  list = DrawListData{};
  list.indices.resize(kDrawIndicesMax + 1);
  CHECK(check_draw_list(list, context_for(textures, 1)) == Refusal::too_large);

  list = DrawListData{};
  list.vertices.resize(3);
  list.indices = {0, 1, 2};
  list.commands.assign(kDrawCommandsMax, command(0, 3, 0, 3));
  CHECK(check_draw_list(list, context_for(textures, 1)) == Refusal::none);
  list.commands.push_back(command(0, 3, 0, 3));
  CHECK(check_draw_list(list, context_for(textures, 1)) == Refusal::too_large);
}

TEST_CASE("a command's ranges: vertices and indices within the list's, whole triangles, no sum that wraps") {
  const std::uint64_t textures[] = {kOwnTexture};
  const DrawContext context = context_for(textures, 1);
  DrawListData list = two_quads();
  list.commands[1].vertex_count = 5;  // one past the list's eight
  CHECK(check_draw_list(list, context) == Refusal::vertex_range);
  list = two_quads();
  list.commands[1].vertex_offset = 0xFFFFFFFFu;  // a sum that would wrap in 32 bits
  CHECK(check_draw_list(list, context) == Refusal::vertex_range);
  list = two_quads();
  list.commands[1].index_offset = 7;  // six indices from 7 end past the list's twelve
  CHECK(check_draw_list(list, context) == Refusal::index_range);
  list = two_quads();
  list.commands[0].index_count = 5;  // not whole triangles
  CHECK(check_draw_list(list, context) == Refusal::index_range);
  list = two_quads();
  list.commands[1].index_offset = 0xFFFFFFFEu;
  CHECK(check_draw_list(list, context) == Refusal::index_range);

  // More than 65,536 vertices in one command: u16 indices cannot reach them, so it is refused.
  list = DrawListData{};
  list.vertices.resize(kDrawVerticesMax);
  list.commands = {command(0, kDrawVerticesMax, 0, 0)};
  CHECK(check_draw_list(list, context) == Refusal::none);  // 13,107 vertices: within a command's reach
  CHECK(kDrawVerticesMax < kCommandVerticesMax);
}

TEST_CASE("an index beyond its own command's vertices is refused, though the list has them") {
  const std::uint64_t textures[] = {kOwnTexture};
  DrawListData list = two_quads();
  list.indices[7] = 4;  // the second command has four vertices: 0 to 3
  CHECK(check_draw_list(list, context_for(textures, 1)) == Refusal::index);
  list = two_quads();
  list.commands[0].vertex_count = 3;  // the first command's index 3 now points past its own vertices
  CHECK(check_draw_list(list, context_for(textures, 1)) == Refusal::index);
}

TEST_CASE("clip rectangles: finite, not inverted, within the panel's bounds") {
  const std::uint64_t textures[] = {kOwnTexture};
  const DrawContext context = context_for(textures, 1);
  const float nan = std::numeric_limits<float>::quiet_NaN();
  const float inf = std::numeric_limits<float>::infinity();
  struct Bad {
    float x0, y0, x1, y1;
  };
  const Bad bad[] = {{-1.0f, 0.0f, 10.0f, 10.0f}, {0.0f, -0.5f, 10.0f, 10.0f}, {0.0f, 0.0f, 200.5f, 10.0f},
                     {0.0f, 0.0f, 10.0f, 101.0f}, {20.0f, 0.0f, 10.0f, 10.0f}, {0.0f, 20.0f, 10.0f, 10.0f},
                     {nan, 0.0f, 10.0f, 10.0f},   {0.0f, 0.0f, inf, 10.0f},    {0.0f, 0.0f, 10.0f, nan}};
  for (const Bad& clip : bad) {
    DrawListData list = two_quads();
    list.commands[1].clip_x0 = clip.x0;
    list.commands[1].clip_y0 = clip.y0;
    list.commands[1].clip_x1 = clip.x1;
    list.commands[1].clip_y1 = clip.y1;
    CHECK(check_draw_list(list, context) == Refusal::clip);
  }
}

TEST_CASE("texture ids: the mod's own, or the shared atlas at the generation the list names") {
  const std::uint64_t textures[] = {kOwnTexture};
  DrawListData list = two_quads();
  list.commands[1].texture = kOtherTexture;  // another mod's, or none
  CHECK(check_draw_list(list, context_for(textures, 1)) == Refusal::texture);
  list.commands[1].texture = 0;
  CHECK(check_draw_list(list, context_for(textures, 1)) == Refusal::texture);
  list.commands[1].texture = kOwnTexture;
  CHECK(check_draw_list(list, context_for(nullptr, 0)) == Refusal::texture);  // registered by nobody now

  list = two_quads();
  list.commands[1].texture = kAtlas;
  list.atlas_generation = 1;
  CHECK(check_draw_list(list, context_for(textures, 1, 1)) == Refusal::none);
  CHECK(check_draw_list(list, context_for(textures, 1, 2)) == Refusal::generation);  // built against an older atlas
  CHECK(check_draw_list(list, context_for(textures, 1, 0)) == Refusal::generation);  // before the atlas is baked
  list.atlas_generation = 0;
  CHECK(check_draw_list(list, context_for(textures, 1, 1)) == Refusal::generation);
  // A list that draws no glyph need not name a generation.
  CHECK(check_draw_list(two_quads(), context_for(textures, 1, 7)) == Refusal::none);
}

TEST_CASE("the triple buffer: the reader gets the newest list, then keeps it until a newer one comes") {
  auto buffers = std::make_unique<DrawListBuffers>();
  CHECK(buffers->read().serial == 0);  // nothing published yet
  for (int i = 1; i <= 3; ++i) {
    const SpinGuard guard(buffers->writer_lock());
    DrawListData& back = buffers->back();
    back.vertices.assign(static_cast<std::size_t>(i), GraceUiDrawVert{});
    buffers->publish();
  }
  DrawListData& newest = buffers->read();
  CHECK(newest.serial == 3);
  CHECK(newest.vertices.size() == 3);
  newest.placed_x = 40.0f;  // the reader's own change stays with it
  CHECK(buffers->read().serial == 3);
  CHECK(buffers->read().placed_x == 40.0f);
  CHECK(buffers->published() == 3);
}

TEST_CASE("the triple buffer under a writer thread: whole lists only, and no allocation or free on the reader's side") {
  auto buffers = std::make_unique<DrawListBuffers>();
  std::atomic<bool> stop{false};
  std::atomic<bool> started{false};
  // Each list's vertices all carry its serial-to-be in their colour, and their count follows from it: a list half
  // written would show two colours or the wrong count.
  std::thread writer([&] {
    for (std::uint32_t n = 1; !stop.load(std::memory_order_relaxed); ++n) {
      const SpinGuard guard(buffers->writer_lock());
      DrawListData& back = buffers->back();
      const std::size_t count = 1 + (n * 7919u) % 2000u;  // varied sizes: the writer's vectors grow and shrink
      back.vertices.assign(count, GraceUiDrawVert{0.0f, 0.0f, 0.0f, 0.0f, n});
      back.indices.assign(count % 3 == 0 ? count : 0, std::uint16_t{0});
      back.placed_x = 0.0f;
      buffers->publish();
      started.store(true, std::memory_order_release);
    }
  });
  while (!started.load(std::memory_order_acquire)) {
    std::this_thread::yield();
  }
  std::uint64_t reads = 0;
  std::uint64_t torn = 0;
  std::uint64_t newer = 0;
  std::uint64_t last = 0;
  test::Counts counts;
  {
    const test::Counting counting;
    for (int i = 0; i < 20000; ++i) {
      DrawListData& list = buffers->read();
      const std::uint32_t n = list.vertices.empty() ? 0 : list.vertices.front().colour;
      const std::size_t want = 1 + (n * 7919u) % 2000u;
      bool whole = list.vertices.size() == want;
      for (const GraceUiDrawVert& vertex : list.vertices) {
        whole = whole && vertex.colour == n;
      }
      torn += whole ? 0 : 1;
      newer += list.serial > last ? 1 : 0;
      CHECK(list.serial >= last);  // never an older list than one already read
      last = list.serial;
      list.placed_x += 1.0f;  // the reader may change what it holds
      ++reads;
    }
    counts = counting.counts();
  }
  stop.store(true);
  writer.join();
  CHECK(reads == 20000);
  CHECK(torn == 0);
  CHECK(newer > 0);
  CHECK(counts.allocations == 0);
  CHECK(counts.frees == 0);
}

TEST_CASE("a submission's shape: counts within their bounds, and a command stride from ABI 1.0's to 256 bytes") {
  CHECK(list_shape_fits(0, 0, 0, kDrawCommandSize10));
  CHECK(list_shape_fits(kDrawVerticesMax, kDrawIndicesMax, kDrawCommandsMax, kDrawCommandSizeMax));
  CHECK_FALSE(list_shape_fits(kDrawVerticesMax + 1, 0, 0, kDrawCommandSize10));
  CHECK_FALSE(list_shape_fits(0, kDrawIndicesMax + 1, 0, kDrawCommandSize10));
  CHECK_FALSE(list_shape_fits(0, 0, kDrawCommandsMax + 1, kDrawCommandSize10));
  CHECK_FALSE(list_shape_fits(0, 0, 1, kDrawCommandSize10 - 1));
  // A stride that is no stride (an unset field, a garbage value) would send the copy gigabytes past the mod's array
  // (the review's L6): refused before anything is read.
  CHECK_FALSE(list_shape_fits(0, 0, 1, kDrawCommandSizeMax + 1));
  CHECK_FALSE(list_shape_fits(0, 0, 1, 0x7FFFFFFFu));
  CHECK_FALSE(list_shape_fits(0, 0, 1, 0xFFFFFFFFu));
  CHECK(kDrawCommandSizeMax == 256u);
}
