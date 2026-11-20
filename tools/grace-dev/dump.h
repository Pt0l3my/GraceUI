// SPDX-License-Identifier: MIT
// dump.h: the in-game view on Linux (§9.2's last point, D168): one page of a space laid out by grace-view at a display's
// size and GraceUI's ui.scale, as the in-game menu lays it out (its effective scale (H / 1080) × ui.scale, §3.13, D167),
// with the space's settings at their defaults; its structure dump (§9.3), and the list grace-draw tessellates from it
// with its counts and the checker's answer (D76). Text is measured in the layout tests' fake face (every glyph 8 px at a
// bake of 16 px), so the dump is the golden tests' own and needs no font file and no Windows.
#pragma once

#include <string>

#include "space_data.h"

namespace grace::dev {

struct DumpRequest {
  std::string page;
  float width = 1920;
  float height = 1080;
  double ui_scale = 1;  // GraceUI's ui.scale, within its own bounds (grace-dev's command line: GraceUI's default)
  bool clear = false;   // Grace Clear's palette (ui.menu.theme)
};

struct DumpResult {
  std::string text;     // a header line, the structure dump, and a line of counts
  bool passes = false;  // the tessellated list passes check_draw_list
};

// False with `why` if the space has no such page.
bool dump_page(const SpaceData& space, const DumpRequest& request, DumpResult& out, std::string& why);

}  // namespace grace::dev
