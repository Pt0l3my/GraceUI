// SPDX-License-Identifier: MIT
// The frame parser under libFuzzer (§9.1, D159): any bytes, both ways. A frame it takes encodes again and parses to the
// same message; nothing throws, and nothing is read past the input.
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

#include "grace/protocol/frame.h"

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
  const std::string text(data, data + size);
  for (const auto direction : {grace::protocol::Direction::from_page, grace::protocol::Direction::to_page}) {
    grace::protocol::Message message;
    grace::protocol::Refusal refusal;
    if (!grace::protocol::parse_frame(text, direction, message, refusal)) {
      continue;
    }
    std::string again;
    if (grace::protocol::encode_frame(message, direction, again)) {
      grace::protocol::Message twice;
      if (!grace::protocol::parse_frame(again, direction, twice, refusal) || !(twice == message)) {
        __builtin_trap();  // a frame taken once must survive its own encoding
      }
    }
  }
  return 0;
}
