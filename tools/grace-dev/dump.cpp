// SPDX-License-Identifier: MIT
// dump.cpp: a page's in-game structure and draw list, without Windows (dump.h).
#include "dump.h"

#include <cstdio>

#include "fake_face.h"  // grace-view's tests' face: the golden dumps' own measures
#include "grace/abi/drawlist.h"
#include "grace/draw/tessellate.h"
#include "grace/pages/settings_store.h"
#include "grace/tokens.h"
#include "grace/view/page_view.h"

namespace grace::dev {
namespace {

std::string fixed(double value, int digits) {
  char text[32];
  std::snprintf(text, sizeof text, "%.*f", digits, value);
  return text;
}

}  // namespace

bool dump_page(const SpaceData& space, const DumpRequest& request, DumpResult& out, std::string& why) {
  view::PageView view;
  if (!view.load(space.pages, space.settings, why)) {
    why = space.origin + ": " + why;
    return false;
  }
  if (!view.open(request.page)) {
    why = space.origin + ": it has no page " + request.page;
    return false;
  }
  pages::SettingsStore store;
  if (!store.load(space.settings, why)) {
    why = space.origin + ": " + why;
    return false;
  }
  if (space.id == "graceui") {
    // GraceUI's own Menu page shows the scale and the theme the menu is drawn at.
    store.set("ui.scale", protocol::Json(request.ui_scale));
    store.set("ui.menu.theme", protocol::Json(request.clear ? "clear" : "grace"));
  }
  view.set_values(store.values());
  const float scale = request.height / 1080.0f * static_cast<float>(request.ui_scale);  // §3.13, as D167 draws
  const view::Fonts fonts = view::test::fonts();
  view::DisplayList list;
  view.layout(view::Viewport{request.width, request.height, scale}, fonts, list);

  constexpr draw::Atlas atlas{view::test::kAtlas, 0.5f / 64, 0.5f / 64};
  abi::DrawListData data;
  const tokens::Palette& palette = request.clear ? tokens::kClear.colour : tokens::kGrace.colour;
  const draw::Stats stats = draw::tessellate(list, palette, atlas, data);
  abi::DrawContext context;
  context.width = request.width;
  context.height = request.height;
  context.atlas = atlas.texture;
  context.atlas_generation = fonts.generation;
  const abi::Refusal refusal = abi::check_draw_list(data, context);
  out.passes = refusal == abi::Refusal::none;

  const std::string size = std::to_string(static_cast<int>(request.width)) + "x" +
                           std::to_string(static_cast<int>(request.height));
  out.text = "# " + space.id + " " + request.page + ": " + size + ", ui.scale " + fixed(request.ui_scale, 2) +
             ", scale " + fixed(scale, 3) + ", theme " + (request.clear ? "clear" : "grace") +
             ", the fake face, settings at their defaults\n";
  out.text += list.dump();
  out.text += "# " + std::to_string(stats.items) + " items, " + std::to_string(stats.vertices) + " vertices, " +
              std::to_string(stats.indices) + " indices, " + std::to_string(stats.commands) +
              " commands; check_draw_list: " +
              (out.passes ? std::string("passes") : std::string("refused (") + abi::refusal_name(refusal) + ")") +
              "\n";
  return true;
}

}  // namespace grace::dev
