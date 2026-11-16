// drawlist_fuzz.cpp: a libFuzzer target for the draw-list checker (§9.1, "Budgets and fuzzing"): any bytes become a list
// (counts, vertices, indices, commands, the atlas's generation) and a context (the panel's size, the mod's textures),
// and the checker must answer without a crash, a read outside the copy or undefined behaviour. When it lets a list
// through, every command's indices are checked again here against what the host will draw.
#include <cstddef>
#include <cstdint>
#include <cstring>

#include "grace/abi/drawlist.h"

namespace {

class Reader {
 public:
  Reader(const std::uint8_t* data, std::size_t size) : data_(data), size_(size) {}
  template <class T>
  T take() {
    T value{};
    if (at_ + sizeof(T) <= size_) {
      std::memcpy(&value, data_ + at_, sizeof(T));
      at_ += sizeof(T);
    } else {
      at_ = size_;
    }
    return value;
  }
  bool done() const { return at_ >= size_; }

 private:
  const std::uint8_t* data_;
  std::size_t size_;
  std::size_t at_ = 0;
};

}  // namespace

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
  Reader in(data, size);
  grace::abi::DrawListData list;
  const std::uint32_t vertices = in.take<std::uint16_t>() % 4096;
  const std::uint32_t indices = in.take<std::uint16_t>() % 8192;
  const std::uint32_t commands = in.take<std::uint8_t>() % 80;
  list.atlas_generation = in.take<std::uint8_t>() % 3;
  list.vertices.resize(vertices);
  for (GraceUiDrawVert& vertex : list.vertices) {
    vertex.x = in.take<float>();
    vertex.y = in.take<float>();
    if (in.done()) {
      break;
    }
  }
  list.indices.resize(indices);
  for (std::uint16_t& index : list.indices) {
    index = in.take<std::uint16_t>();
  }
  list.commands.resize(commands);
  for (GraceUiDrawCommand& command : list.commands) {
    command = in.take<GraceUiDrawCommand>();
  }
  const std::uint64_t textures[] = {0x102, 0x203};
  grace::abi::DrawContext context;
  context.width = static_cast<float>(in.take<std::uint16_t>() % 4096);
  context.height = static_cast<float>(in.take<std::uint16_t>() % 4096);
  context.textures = textures;
  context.texture_count = in.take<std::uint8_t>() % 3;
  context.atlas = 1;
  context.atlas_generation = in.take<std::uint8_t>() % 3;
  if (grace::abi::check_draw_list(list, context) == grace::abi::Refusal::none) {
    for (const GraceUiDrawCommand& command : list.commands) {
      for (std::uint32_t i = 0; i < command.index_count; ++i) {
        const std::uint32_t vertex = command.vertex_offset + list.indices[command.index_offset + i];
        if (vertex >= list.vertices.size()) {
          __builtin_trap();  // a list the checker let through would read outside its vertices
        }
      }
    }
  }
  return 0;
}
