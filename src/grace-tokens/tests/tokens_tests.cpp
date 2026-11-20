// The generated header (tools/tokens/build.py): the values the in-game view draws with are the design's, the mixes
// composite as the tool composites them, and Grace Clear differs where §3.3 says.
#include <doctest/doctest.h>

#include <string_view>

#include "grace/tokens.h"

namespace {

using namespace grace::tokens;

static_assert(kGrace.colour.ground_2.rgb() == 0x1D1A15 && kGrace.colour.ink_hi.rgb() == 0xE8E2D0,
              "§3.2's values, at compile time");
static_assert(over(kGrace.colour.rule, kGrace.colour.ground_2).rgb() == 0x352E1F,
              "rule over ground.2, as 0.1's notices draw their border");

}  // namespace

TEST_CASE("the palette is the design's") {
  CHECK(kGrace.colour.gold.rgb() == 0xC9A65A);
  CHECK(kGrace.colour.ember.rgb() == 0xDB9550);
  CHECK(kGrace.colour.rot.rgb() == 0xD9806F);
  CHECK(kGrace.colour.gold.a == 255);
  CHECK(kGrace.colour.rule.a == 36);         // gold at 14%, as a byte
  CHECK(kGrace.colour.tint_select.a == 26);  // gold at 10%
  CHECK(kGrace.colour.scrim.rgb() == kGrace.colour.ground_0.rgb());
}

TEST_CASE("Grace Clear differs where §3.3 says, and only there") {
  CHECK(kClear.colour.ground_well.rgb() == 0x000000);
  CHECK(kClear.colour.ink_hi.rgb() == 0xF4EFE2);
  CHECK(kClear.colour.ink_faint == kClear.colour.ink_mute);
  CHECK(kClear.colour.gold == kGrace.colour.gold);
  CHECK(kClear.size.edge_width == 2.0f);
  CHECK(kClear.size.focus_width == 3.0f);
  CHECK(kGrace.size.focus_width == 2.0f);
}

TEST_CASE("type, spacing and motion") {
  CHECK(type::ui.size == 15.0f);
  CHECK(type::ui.line == 22.0f);
  CHECK(type::eyebrow.upper);
  CHECK(type::display_lg.font == Font::display);
  CHECK(kFontFamily[static_cast<int>(Font::sans)] == std::string_view("Noto Sans"));
  CHECK(size::settings_control == 280.0f);
  CHECK(dur::close.ms == 200);
  CHECK(dur::close.reduced_ms == 60);   // a fade, at most 60 ms with reduced motion
  CHECK(dur::state.reduced_ms == 0);    // a transition: none
  CHECK(dur::close_danger.ms == 0);
}
