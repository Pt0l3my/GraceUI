// SPDX-License-Identifier: MIT
// typed.h: a number typed into a slider's field (§3.8, D266), as both views read it: the text the field opens with, and
// what a typed text becomes, a value the settings store will take or the words that refuse it. Pure: no ImGui, no
// Win32. The browser view's web/src/model/typed.ts follows the same rules, checked against fixtures/typed-cases.json.
#pragma once

#include <string>
#include <string_view>

#include "grace/protocol/messages.h"

namespace grace::view {

// The decimals a step shows: 0.05 two, 0.5 one, 1 none (at most four).
int step_decimals(double step);

// A number as the menu shows it: the step's decimals, then the unit ("1.25×", "2.5 s", "8642").
std::string format_number(const protocol::Setting& s, double n);

// The text a slider's field opens with: the value at its step's decimals, without its unit ("1.25").
std::string plain_number(const protocol::Setting& s, double n);

// The most a slider's field holds, in bytes.
inline constexpr std::size_t kTypedNumberMax = 32;

struct Typed {
  bool ok = false;      // `value` is ready for settings.set
  protocol::Json value;  // on the step's grid from the minimum (or 0), within the bounds; an integer's whole
  std::string words;     // not ok: why, in the player's words ("Type a number from 0.75× to 1.50×."); empty, nothing
};

// What a typed text becomes for an integer, a number or a duration (§3.8, D266). Spaces at either end, and the unit
// at the end, are ignored ("×" also as x or X); the rest is an optional sign and digits with at most one decimal point,
// a full stop or a comma. Empty: nothing. Not a number, or outside the bounds: refused with the bounds in words. Else
// snapped to the nearest step from the minimum (or 0), kept within the bounds.
Typed typed_value(const protocol::Setting& s, std::string_view text);

}  // namespace grace::view
