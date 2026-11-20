// SPDX-License-Identifier: MIT
// space_data.h: a space grace-dev serves (§9.2, D168): a built-in one by its id (grace-pages' page data and settings
// description, with its fixtures from fixtures/<id>/ compiled into grace-dev), or an author's from a folder holding
// pages.json, settings.json and, optionally, topics.json and scenario.json. Each passes the protocol's decoders, the
// settings store's and the page view's own loads, and grace-dev's checks of its topics, commands and fixtures before it
// is served; a failure names the file and the field, never a folder's path (PV3).
#pragma once

#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "grace/protocol/messages.h"

namespace grace::dev {

// The bounds of an author's files (EST): page data and a settings description well past any space's, and fixtures and
// a scenario as large as a few full state frames.
inline constexpr std::size_t kMaxPagesFile = 1024 * 1024;
inline constexpr std::size_t kMaxFixtureFile = 4 * 1024 * 1024;
// A state's body or a batch of rows, with room left in the 256 KiB frame for its envelope.
inline constexpr std::size_t kMaxBody = protocol::kFrameToPage - 1024;
// A stream with no ring of its own keeps as many rows as grace-serve keeps for it (bridge.cpp's default).
inline constexpr std::size_t kDefaultRing = 200;

// A fixture file compiled into grace-dev (embedded.cpp, written by CMake from fixtures/<space>/).
struct EmbeddedFixture {
  std::string_view space;
  std::string_view file;  // "topics.json" or "scenario.json"
  std::string_view text;
};
std::span<const EmbeddedFixture> embedded_fixtures() noexcept;

struct SpaceData {
  std::string id;
  std::string origin;  // how messages name it: "the built-in space demo", "the space folder 'lanterns'"
  bool builtin = false;
  protocol::SpacePages pages;
  protocol::SettingsDescription settings;
  protocol::Json fixtures = protocol::Json::object();  // a state topic's body, or a stream topic's rows
  std::string scenario_name;  // its own scenario's file, for messages ("fixtures/demo/scenario.json"); empty if none
  std::string scenario_text;

  const protocol::TopicInfo* topic(std::string_view id) const noexcept;
  bool has_command(std::string_view name) const noexcept;
  const protocol::Setting* setting(std::string_view key) const noexcept;
  bool has_page(std::string_view id) const noexcept;
};

// The built-in spaces' ids, GraceUI's own first.
std::vector<std::string> builtin_ids();

bool load_builtin(std::string_view id, SpaceData& out, std::string& why);
bool load_folder(const std::filesystem::path& folder, SpaceData& out, std::string& why);

// A ring's size: its own, or kDefaultRing.
std::size_t ring_of(const protocol::TopicInfo& topic) noexcept;

// GraceUI's own setting `key`, from its built-in description (the port's and the scale's defaults and bounds).
const protocol::Setting& graceui_setting(std::string_view key);
// dashboard.port's default (D58).
std::uint16_t default_port();
// GraceUI's ui.scale as its own space describes its default (§3.13): --dump's scale unless --scale names one.
double default_ui_scale();

}  // namespace grace::dev
