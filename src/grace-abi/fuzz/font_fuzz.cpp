// font_fuzz.cpp: a libFuzzer target for the checks a mod's font file meets on GraceUI's own copy (registry.h): any bytes
// become a font file and a face's ranges, and valid_font, valid_ranges and mapped_code_points must answer without a
// crash, a read outside the copy or undefined behaviour; a count of mapped code points never exceeds the ranges' own.
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <vector>

#include "grace/abi/registry.h"

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
  // The first byte says how many range pairs follow (at most 8), then the pairs, then the font file.
  if (size == 0) {
    return 0;
  }
  const std::size_t pairs = data[0] % 9;
  std::size_t at = 1;
  std::vector<std::uint32_t> ranges;
  std::uint64_t points = 0;
  for (std::size_t i = 0; i < pairs && at + 8 <= size; ++i, at += 8) {
    std::uint32_t first = 0;
    std::uint32_t last = 0;
    std::memcpy(&first, data + at, 4);
    std::memcpy(&last, data + at + 4, 4);
    first = 0x20 + first % 0x10FFE0;
    last = first + last % 0x800;  // short ranges: the fuzzer's time goes to the map, not to counting
    ranges.push_back(first);
    ranges.push_back(last);
    points += std::uint64_t{last} - first + 1;
  }
  ranges.push_back(0);
  // The file in a buffer of its own size, so that AddressSanitizer catches a read past its end.
  std::vector<unsigned char> font(data + (at < size ? at : size), data + size);
  (void)grace::abi::valid_font(font.data(), font.size());
  if (ranges.size() > 1) {
    (void)grace::abi::valid_ranges(ranges.data(), (ranges.size() - 1) / 2);
    const std::uint64_t mapped = grace::abi::mapped_code_points(font.data(), font.size(), ranges.data());
    if (mapped > points) {
      std::abort();
    }
  }
  (void)grace::abi::mapped_code_points(font.data(), font.size(), nullptr);
  return 0;
}
