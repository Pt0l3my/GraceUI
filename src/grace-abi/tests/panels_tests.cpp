// The panels mods draw (panels.h): their descriptions, their places on the display by anchor, their life (hidden at
// first, shown and hidden by their mod, gone with it), the bounds per mod and in all, and the present thread's view of a
// slot that a mod's thread is making or removing at that moment.
#include <doctest/doctest.h>

#include <atomic>
#include <cmath>
#include <limits>
#include <memory>
#include <thread>

#include "grace/abi/panels.h"

using namespace grace::abi;

namespace {

constexpr GraceUiConsumer kModA = (1ull << 8) | 1;
constexpr GraceUiConsumer kModB = (1ull << 8) | 2;

PanelDesc desc(std::uint32_t anchor = GRACEUI_ANCHOR_TOP_LEFT, float x = 10.0f, float y = 20.0f, float width = 100.0f,
               float height = 50.0f) {
  PanelDesc d;
  d.layer = GRACEUI_LAYER_PANEL;
  d.anchor = anchor;
  d.x = x;
  d.y = y;
  d.width = width;
  d.height = height;
  return d;
}

}  // namespace

TEST_CASE("a panel's description: a known layer, anchor and flags, finite offsets, a size of 1 to 16384") {
  CHECK(valid_panel(desc()));
  PanelDesc d = desc();
  d.flags = GRACEUI_PANEL_TAKES_INPUT;
  CHECK(valid_panel(d));
  for (const std::uint32_t layer : {GRACEUI_LAYER_HUD, GRACEUI_LAYER_PANEL, GRACEUI_LAYER_MODAL}) {
    d = desc();
    d.layer = layer;
    CHECK(valid_panel(d));
  }
  for (const std::uint32_t layer : {0u, 3u, 5u, 8u}) {  // 3: GraceUI's menu, never a mod's
    d = desc();
    d.layer = layer;
    CHECK_FALSE(valid_panel(d));
  }
  d = desc();
  d.anchor = 9;
  CHECK_FALSE(valid_panel(d));
  d = desc();
  d.flags = 2;
  CHECK_FALSE(valid_panel(d));
  const float nan = std::numeric_limits<float>::quiet_NaN();
  CHECK_FALSE(valid_panel(desc(0, nan)));
  CHECK_FALSE(valid_panel(desc(0, 0.0f, 20000.0f)));
  CHECK_FALSE(valid_panel(desc(0, 0.0f, 0.0f, 0.5f)));
  CHECK_FALSE(valid_panel(desc(0, 0.0f, 0.0f, 100.0f, 16385.0f)));
  CHECK_FALSE(valid_panel(desc(0, 0.0f, 0.0f, std::numeric_limits<float>::infinity())));
  CHECK(valid_panel(desc(0, -30.0f, -40.0f)));  // a panel may hang off an edge: drawing is clipped to the display
}

TEST_CASE("a panel's place on the display, by each of the nine anchors") {
  struct Case {
    std::uint32_t anchor;
    float x0, y0;
  };
  // 1280 x 720, a panel 100 x 50 moved 10 inwards horizontally and 20 vertically.
  const Case cases[] = {
      {GRACEUI_ANCHOR_TOP_LEFT, 10.0f, 20.0f},     {GRACEUI_ANCHOR_TOP, 600.0f, 20.0f},
      {GRACEUI_ANCHOR_TOP_RIGHT, 1170.0f, 20.0f},  {GRACEUI_ANCHOR_LEFT, 10.0f, 355.0f},
      {GRACEUI_ANCHOR_CENTRE, 600.0f, 355.0f},     {GRACEUI_ANCHOR_RIGHT, 1170.0f, 355.0f},
      {GRACEUI_ANCHOR_BOTTOM_LEFT, 10.0f, 650.0f}, {GRACEUI_ANCHOR_BOTTOM, 600.0f, 650.0f},
      {GRACEUI_ANCHOR_BOTTOM_RIGHT, 1170.0f, 650.0f},
  };
  for (const Case& c : cases) {
    const Rect rect = place(desc(c.anchor), 1280.0f, 720.0f);
    CAPTURE(c.anchor);
    CHECK(rect.x0 == doctest::Approx(c.x0));
    CHECK(rect.y0 == doctest::Approx(c.y0));
    CHECK(rect.x1 - rect.x0 == doctest::Approx(100.0f));
    CHECK(rect.y1 - rect.y0 == doctest::Approx(50.0f));
  }
  // The display changes size: an anchored panel follows its anchor.
  const Rect smaller = place(desc(GRACEUI_ANCHOR_BOTTOM_RIGHT), 800.0f, 450.0f);
  CHECK(smaller.x0 == doctest::Approx(690.0f));
  CHECK(smaller.y0 == doctest::Approx(380.0f));
}

TEST_CASE("panels start hidden, are shown and hidden by their own mod only, and go with it") {
  auto panels = std::make_unique<Panels>();
  std::uint64_t a = 0;
  std::uint64_t b = 0;
  REQUIRE(panels->create(kModA, desc(), a) == PanelResult::ok);
  REQUIRE(panels->create(kModB, desc(GRACEUI_ANCHOR_CENTRE), b) == PanelResult::ok);
  CHECK(a != 0);
  CHECK(a != b);
  CHECK(panels->count() == 2);
  CHECK(panels->visible() == 0);  // hidden at first (D76)

  CHECK(panels->set_visible(kModA, a, true) == PanelResult::ok);
  CHECK(panels->set_visible(kModB, a, true) == PanelResult::unknown);  // another mod's panel
  CHECK(panels->set_visible(kModA, 0, true) == PanelResult::unknown);
  CHECK(panels->set_visible(kModA, a + 1, true) == PanelResult::unknown);
  CHECK(panels->visible() == 1);

  PanelDesc found;
  std::uint32_t index = 0;
  CHECK(panels->find(kModA, a, found, index) == PanelResult::ok);
  CHECK(found.width == 100.0f);
  CHECK(panels->find(kModB, a, found, index) == PanelResult::unknown);

  PanelView view;
  REQUIRE(panels->view(static_cast<std::uint32_t>((a & 0xFFu) - 1), view));
  CHECK(view.id == a);
  CHECK(view.owner == kModA);
  CHECK(view.visible);

  CHECK(panels->remove_owner(kModA) == 1);
  CHECK(panels->count() == 1);
  CHECK(panels->set_visible(kModA, a, true) == PanelResult::unknown);
  CHECK_FALSE(panels->view(static_cast<std::uint32_t>((a & 0xFFu) - 1), view));

  // The slot is used again: the old id names nothing, the new panel starts hidden.
  std::uint64_t again = 0;
  REQUIRE(panels->create(kModB, desc(), again) == PanelResult::ok);
  CHECK((again & 0xFFu) == (a & 0xFFu));
  CHECK(again != a);
  CHECK(panels->set_visible(kModA, a, false) == PanelResult::unknown);
  CHECK(panels->find(kModB, a, found, index) == PanelResult::unknown);
  CHECK(panels->visible() == 0);
}

TEST_CASE("bounds: GRACEUI_PANELS_PER_MOD a mod, GRACEUI_PANELS_MAX in all; a bad description is refused") {
  auto panels = std::make_unique<Panels>();
  std::uint64_t id = 0;
  for (std::uint32_t i = 0; i < kPanelsPerMod; ++i) {
    CHECK(panels->create(kModA, desc(), id) == PanelResult::ok);
  }
  CHECK(panels->create(kModA, desc(), id) == PanelResult::full);
  CHECK(panels->create(0, desc(), id) == PanelResult::invalid);
  PanelDesc bad = desc();
  bad.layer = 3;
  CHECK(panels->create(kModB, bad, id) == PanelResult::invalid);
  for (std::uint32_t mod = 2; panels->count() < kMaxPanels; ++mod) {
    for (std::uint32_t i = 0; i < kPanelsPerMod && panels->count() < kMaxPanels; ++i) {
      REQUIRE(panels->create((1ull << 8) | mod, desc(), id) == PanelResult::ok);
    }
  }
  CHECK(panels->create(0xFF01, desc(), id) == PanelResult::full);
}

TEST_CASE("the present thread's view while mods' threads make and remove panels: consistent, never waiting") {
  auto panels = std::make_unique<Panels>();
  std::atomic<bool> stop{false};
  std::atomic<std::uint32_t> made{0};
  std::thread writer([&] {
    for (std::uint32_t n = 1; !stop.load(std::memory_order_relaxed); ++n) {
      std::uint64_t id = 0;
      // Each panel's description is made of one number: a view that mixed two panels would show two.
      const float side = static_cast<float>(1 + n % 1000);
      PanelDesc d = desc(GRACEUI_ANCHOR_TOP_LEFT, side, side, side, side);
      if (panels->create(kModA, d, id) == PanelResult::ok) {
        panels->set_visible(kModA, id, (n & 1u) != 0);
        made.fetch_add(1, std::memory_order_relaxed);
      }
      if (n % 3 == 0) {
        panels->remove_owner(kModA);
      }
    }
  });
  std::uint64_t seen = 0;
  for (int round = 0; round < 20000 || (seen == 0 && round < 20000000); ++round) {
    if (made.load(std::memory_order_relaxed) == 0) {
      std::this_thread::yield();  // until the writer has begun
    }
    for (std::uint32_t i = 0; i < kMaxPanels; ++i) {
      PanelView view;
      if (panels->view(i, view)) {
        ++seen;
        CHECK(view.desc.x == view.desc.width);
        CHECK(view.desc.y == view.desc.height);
        CHECK(view.desc.x == view.desc.y);
      }
    }
  }
  stop.store(true);
  writer.join();
  CHECK(seen > 0);
}

TEST_CASE("shown: the mod's own panel, shown and alive, read under the panels' lock") {
  auto panels = std::make_unique<Panels>();
  std::uint64_t a = 0;
  REQUIRE(panels->create(kModA, desc(), a) == PanelResult::ok);
  CHECK_FALSE(panels->shown(kModA, a));  // hidden at first
  REQUIRE(panels->set_visible(kModA, a, true) == PanelResult::ok);
  CHECK(panels->shown(kModA, a));
  CHECK_FALSE(panels->shown(kModB, a));  // another mod's view of it: not its panel
  REQUIRE(panels->set_visible(kModA, a, false) == PanelResult::ok);
  CHECK_FALSE(panels->shown(kModA, a));
  REQUIRE(panels->set_visible(kModA, a, true) == PanelResult::ok);
  CHECK(panels->remove_owner(kModA) == 1);
  CHECK_FALSE(panels->shown(kModA, a));  // gone with its mod
  CHECK_FALSE(panels->shown(kModA, 0));
}
