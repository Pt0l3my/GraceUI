// drawlist.cpp: a panel's draw lists (drawlist.h).
#include "grace/abi/drawlist.h"

#include <cmath>

namespace grace::abi {

const char* refusal_name(Refusal refusal) noexcept {
  switch (refusal) {
    case Refusal::none: return "none";
    case Refusal::too_large: return "more vertices, indices or commands than a list may hold";
    case Refusal::vertex_range: return "a command's vertices outside the list's";
    case Refusal::index_range: return "a command's indices outside the list's, or not whole triangles";
    case Refusal::index: return "an index beyond its command's vertices";
    case Refusal::clip: return "a clip rectangle outside the panel's bounds";
    case Refusal::texture: return "a texture that is neither the mod's nor the shared atlas";
    case Refusal::generation: return "the shared atlas at another generation";
  }
  return "refused";
}

namespace {

bool texture_known(std::uint64_t texture, const DrawContext& context) noexcept {
  for (std::uint32_t i = 0; i < context.texture_count; ++i) {
    if (context.textures[i] == texture) {
      return true;
    }
  }
  return false;
}

bool clip_within(const GraceUiDrawCommand& command, const DrawContext& context) noexcept {
  const float x0 = command.clip_x0;
  const float y0 = command.clip_y0;
  const float x1 = command.clip_x1;
  const float y1 = command.clip_y1;
  return std::isfinite(x0) && std::isfinite(y0) && std::isfinite(x1) && std::isfinite(y1) && x0 >= 0.0f &&
         y0 >= 0.0f && x0 <= x1 && y0 <= y1 && x1 <= context.width && y1 <= context.height;
}

}  // namespace

Refusal check_draw_list(const DrawListData& list, const DrawContext& context) noexcept {
  const std::size_t vertex_count = list.vertices.size();
  const std::size_t index_count = list.indices.size();
  if (vertex_count > kDrawVerticesMax || index_count > kDrawIndicesMax || list.commands.size() > kDrawCommandsMax) {
    return Refusal::too_large;
  }
  bool uses_atlas = false;
  for (const GraceUiDrawCommand& command : list.commands) {
    // Ranges first, in 64 bits, so that no sum can wrap.
    const std::uint64_t vertex_end = std::uint64_t{command.vertex_offset} + command.vertex_count;
    if (vertex_end > vertex_count || command.vertex_count > kCommandVerticesMax) {
      return Refusal::vertex_range;
    }
    const std::uint64_t index_end = std::uint64_t{command.index_offset} + command.index_count;
    if (index_end > index_count || command.index_count % 3 != 0) {
      return Refusal::index_range;
    }
    for (std::uint32_t i = 0; i < command.index_count; ++i) {
      if (list.indices[command.index_offset + i] >= command.vertex_count) {
        return Refusal::index;
      }
    }
    if (!clip_within(command, context)) {
      return Refusal::clip;
    }
    if (command.texture == context.atlas && context.atlas != 0) {
      uses_atlas = true;
    } else if (!texture_known(command.texture, context)) {
      return Refusal::texture;
    }
  }
  if (uses_atlas && (context.atlas_generation == 0 || list.atlas_generation != context.atlas_generation)) {
    return Refusal::generation;
  }
  return Refusal::none;
}

void DrawListBuffers::publish() noexcept {
  buffers_[back_].serial = published_.fetch_add(1, std::memory_order_relaxed) + 1;
  back_ = middle_.exchange(back_ | kFresh, std::memory_order_acq_rel) & ~kFresh;
}

DrawListData& DrawListBuffers::read() noexcept {
  if ((middle_.load(std::memory_order_relaxed) & kFresh) != 0) {
    front_ = middle_.exchange(front_, std::memory_order_acq_rel) & ~kFresh;
  }
  return buffers_[front_];
}

}  // namespace grace::abi
