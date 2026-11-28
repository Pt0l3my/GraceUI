// SPDX-License-Identifier: MIT
// hello_space.cpp: the fourth example mod of our own (D85, D283), in C++: a space of its own, with a page in both
// views, its topics published and its commands checked and answered. Everything about it is invented. It shows the
// shape every mod with pages follows:
//
//   - Its pages are data: space/space.toml, compiled at the mod's build by GraceUI's tools/pages/build.py --build,
//     which runs GraceUI's page lint and writes hello_space_data.h, the page data and settings description that
//     register_pages takes. The mod writes no HTML, no script and no stylesheet.
//   - Nothing in DllMain but starting a thread of its own; GraceUI is found and called from that thread (D70).
//   - It builds a topic only while a view watches it (GRACEUI_EVENT_WATCHED to _UNWATCHED), and publishes each change
//     as a whole state, or as new rows of its stream (§2.4).
//   - Its one entry point checks every command, whichever view sent it: the name one of its own, the arguments of
//     their types and in their bounds, a destructive one confirmed; anything else is refused with its reason in words
//     (invariant 3). GraceUI refuses some first (a command the space does not declare, a destructive action's without
//     "confirm": true), but a setting's type and bounds are the space's to check. GraceUI routes the answer to the view
//     that asked.
//   - It waits on GraceUI's wake event, never on a timer, and polls until nothing is left (D212).
//
// tools/grace-harness loads it and changes the count from the in-game menu and from the browser (hello_space.h).
#include <windows.h>

#include <cstdint>
#include <cstdio>
#include <deque>
#include <string>
#include <string_view>

#include <nlohmann/json.hpp>

#include "graceui_sys.hpp"
#include "hello_space.h"
#include "hello_space_data.h"

namespace {

using Json = nlohmann::json;

constexpr std::int64_t kStepMin = 1;
constexpr std::int64_t kStepMax = 10;
constexpr std::int64_t kStepDefault = 1;
constexpr bool kLogDefault = true;
constexpr std::size_t kRowsKept = 50;  // the log's ring in space.toml

std::string text_of(const Json& value) {
  return value.dump(-1, ' ', false, Json::error_handler_t::replace);
}

// Where a command came from, in the words the page reads ("view", protocol 1.8).
const char* where(std::string_view view) {
  if (view == "game") return "in game";
  if (view == "browser") return "in the browser";
  if (view == "device") return "on a paired device";
  return "somewhere";
}

class Space {
 public:
  Space(const graceui::Api& api, GraceUiConsumer me) : api_(api), me_(me) {
    const Json description = Json::parse(std::string_view(HELLO_SPACE_SETTINGS, HELLO_SPACE_SETTINGS_LENGTH), nullptr,
                                         false);
    if (description.is_object()) {
      schema_ = description.value("schema", Json::object());
      settings_ = description.value("settings", Json::array());
    }
    started_ = GetTickCount64();
  }

  // One event from poll: a watch, an unwatch or a command.
  void handle(const GraceUiEvent& event) {
    const Json frame =
        Json::parse(std::string_view(event.frame, static_cast<std::size_t>(event.frame_length)), nullptr, false);
    if (!frame.is_object()) {
      return;  // GraceUI checked the frame already; a mod still never trusts one
    }
    const std::string topic = frame.contains("topic") && frame["topic"].is_string() ? frame["topic"].get<std::string>()
                                                                                    : std::string();
    if (event.kind == GRACEUI_EVENT_WATCHED) {
      watched(topic, true);
      if (topic == HELLO_SPACE_LOG) {
        const std::uint64_t since =
            frame.contains("since") && frame["since"].is_number_unsigned() ? frame["since"].get<std::uint64_t>() : 0;
        publish_rows(since);
      } else {
        publish(topic);
      }
    } else if (event.kind == GRACEUI_EVENT_UNWATCHED) {
      watched(topic, false);  // nothing more is built for it until a view watches it again
    } else if (event.kind == GRACEUI_EVENT_COMMAND) {
      api_.answer(me_, event.reply, text_of(command(frame)));
    }
  }

 private:
  // The one entry point (invariant 3): it checks the command, does it, and gives its result frame.
  Json command(const Json& frame) {
    const Json args = frame.contains("args") && frame["args"].is_object() ? frame["args"] : Json::object();
    const std::string name = frame.contains("name") && frame["name"].is_string() ? frame["name"].get<std::string>() : "";
    const std::string view = frame.contains("view") && frame["view"].is_string() ? frame["view"].get<std::string>() : "";
    Json result = {{"v", 1}, {"t", "result"}, {"space", HELLO_SPACE_ID}, {"id", frame.value("id", Json(0))}};
    const auto refuse = [&](const char* error, const char* detail) {
      result["error"] = error;
      result["detail"] = detail;
      return result;
    };
    if (name == HELLO_SPACE_ADD) {
      count_ += step_;
      last_ = view;
      note("Added " + std::to_string(step_) + " " + where(view) + ": " + std::to_string(count_) + ".");
      publish(HELLO_SPACE_STATUS);
    } else if (name == HELLO_SPACE_RESET) {
      if (!(args.contains("confirm") && args["confirm"].is_boolean() && args["confirm"].get<bool>())) {
        return refuse("confirm_required", HELLO_SPACE_CONFIRM_FIRST);  // a destructive command asks first
      }
      count_ = 0;
      last_ = view;
      note(std::string("Reset ") + where(view) + ".");
      publish(HELLO_SPACE_STATUS);
    } else if (name == "settings.set") {
      const std::string key = args.contains("key") && args["key"].is_string() ? args["key"].get<std::string>() : "";
      const Json value = args.contains("value") ? args["value"] : Json();
      if (key == HELLO_SPACE_STEP) {
        if (!value.is_number_integer() || value.get<std::int64_t>() < kStepMin || value.get<std::int64_t>() > kStepMax) {
          return refuse("invalid_value", HELLO_SPACE_STEP_REFUSED);
        }
        step_ = value.get<std::int64_t>();
      } else if (key == HELLO_SPACE_LOG_CHANGES) {
        if (!value.is_boolean()) {
          return refuse("invalid_value", "This is on or off.");
        }
        log_ = value.get<bool>();
      } else {
        return refuse("unknown_key", "Hello space has no such setting.");
      }
      settings_changed();
    } else if (name == "settings.reset") {
      const std::string scope = args.contains("scope") && args["scope"].is_string() ? args["scope"].get<std::string>()
                                                                                    : "";
      const std::string target = args.contains("target") && args["target"].is_string() ? args["target"].get<std::string>()
                                                                                       : "";
      const bool confirmed = args.contains("confirm") && args["confirm"].is_boolean() && args["confirm"].get<bool>();
      if (scope == "key" && target == HELLO_SPACE_STEP) {
        step_ = kStepDefault;
      } else if (scope == "key" && target == HELLO_SPACE_LOG_CHANGES) {
        log_ = kLogDefault;
      } else if ((scope == "page" && target == HELLO_SPACE_PAGE) || scope == "all") {
        if (!confirmed) {
          return refuse("confirm_required", "Putting every setting back asks first.");
        }
        step_ = kStepDefault;
        log_ = kLogDefault;
      } else {
        return refuse("unknown_key", "Hello space has no such setting or page.");
      }
      settings_changed();
    } else {
      return refuse("unknown_command", "Hello space has no such command.");
    }
    result["ok"] = true;
    return result;
  }

  void settings_changed() {
    publish("settings");
    publish(HELLO_SPACE_STATUS);  // its sentence names the step
  }

  // A line in the log, kept in the mod's own ring and published while a view watches it.
  void note(const std::string& line) {
    if (!log_) {
      return;
    }
    const std::uint64_t seconds = (GetTickCount64() - started_) / 1000;
    char time[32];
    std::snprintf(time, sizeof time, "%llu:%02llu", static_cast<unsigned long long>(seconds / 60),
                  static_cast<unsigned long long>(seconds % 60));
    rows_.push_back({{"seq", ++seq_}, {"time", time}, {"line", line}});
    if (rows_.size() > kRowsKept) {
      rows_.pop_front();
      ++dropped_;
    }
    if (log_watched_) {
      publish_rows(seq_ - 1);
    }
  }

  void watched(const std::string& topic, bool on) {
    if (topic == "settings") settings_watched_ = on;
    if (topic == HELLO_SPACE_STATUS) status_watched_ = on;
    if (topic == HELLO_SPACE_LOG) log_watched_ = on;
  }

  // A state topic's whole body, published only while a view watches it.
  void publish(const std::string& topic) {
    Json body;
    if (topic == "settings" && settings_watched_) {
      Json values = {{HELLO_SPACE_STEP, step_}, {HELLO_SPACE_LOG_CHANGES, log_}};
      Json changed = Json::array();
      if (step_ != kStepDefault) changed.push_back(HELLO_SPACE_STEP);
      if (log_ != kLogDefault) changed.push_back(HELLO_SPACE_LOG_CHANGES);
      body = {{"schema", schema_}, {"description", settings_}, {"values", values}, {"changed", changed}};
    } else if (topic == HELLO_SPACE_STATUS && status_watched_) {
      const std::string count = std::to_string(count_);
      std::string sentence = last_.empty() ? "The count is " + count + ", and nothing has changed it yet."
                                           : "The count is " + count + ", last changed " + where(last_) + ".";
      sentence += " Add puts " + std::to_string(step_) + " on it.";
      const char* last = last_ == "game" ? "In game" : last_ == "browser" ? "In the browser"
                       : last_ == "device" ? "On a paired device" : "None yet";
      body = {{"tiles", Json::array({{{"label", "Count"}, {"value", count}}, {{"label", "Last change"}, {"value", last}}})},
              {"sentence", sentence}};
    } else {
      return;  // not watched, or not a state topic of this space's: nothing is built
    }
    const Json frame = {{"v", 1}, {"t", "state"}, {"space", HELLO_SPACE_ID}, {"topic", topic}, {"rev", ++rev_},
                        {"body", body}};
    api_.publish(me_, text_of(frame));
  }

  // The log's rows after `since`, in batches of at most 100, with the rows its ring has lost.
  void publish_rows(std::uint64_t since) {
    Json rows = Json::array();
    for (const Json& row : rows_) {
      if (row["seq"].get<std::uint64_t>() > since) {
        rows.push_back(row);
      }
    }
    if (rows.empty()) {
      return;
    }
    const Json frame = {{"v", 1},          {"t", "rows"},    {"space", HELLO_SPACE_ID},
                        {"topic", HELLO_SPACE_LOG}, {"from_seq", rows.front()["seq"]}, {"rows", rows},
                        {"dropped", dropped_}};
    api_.publish(me_, text_of(frame));
  }

  const graceui::Api& api_;
  GraceUiConsumer me_;
  Json schema_ = Json::object();
  Json settings_ = Json::array();
  std::uint64_t started_ = 0;
  std::int64_t count_ = 0;
  std::string last_;  // the view that changed it last, "" before any
  std::int64_t step_ = kStepDefault;
  bool log_ = kLogDefault;
  std::uint64_t rev_ = 0;
  std::uint64_t seq_ = 0;
  std::uint64_t dropped_ = 0;
  std::deque<Json> rows_;
  bool settings_watched_ = false;
  bool status_watched_ = false;
  bool log_watched_ = false;
};

char g_frame[GRACEUI_FRAME_FROM_PAGE_MAX];  // always room for a view's frame

DWORD WINAPI hello_worker(LPVOID) {
  const graceui::Api api = graceui::Api::find();
  if (!api || !api.has_cap(GRACEUI_CAP_PAGES)) {
    return 0;  // no GraceUI, or one without pages: this mod shows nothing
  }
  GraceUiConnectAnswer answer{};
  // Its floor: GraceUI 0.2.70, whose protocol 1.22 is the minor its page data names (--minor 22: its rail's icon).
  const graceui::Mod mod{HELLO_SPACE_ID, HELLO_SPACE_NAME, GRACEUI_VERSION_PACK(1, 0, 0), GRACEUI_VERSION_PACK(0, 2, 70),
                         "pages wake"};
  if (api.connect(mod, answer) != GRACEUI_OK) {
    return 0;  // too old, refused, or inactive this session (an anti-cheat): no pages to show
  }
  const GraceUiConsumer me = answer.consumer;
  const GraceUiStatus registered =
      api.register_pages(me, GRACEUI_VIEW_GAME | GRACEUI_VIEW_BROWSER, GRACEUI_VERSION_PACK(1, 0, 0),
                         std::string_view(HELLO_SPACE_PAGES, HELLO_SPACE_PAGES_LENGTH),
                         std::string_view(HELLO_SPACE_SETTINGS, HELLO_SPACE_SETTINGS_LENGTH), "0.2");
  if (registered != GRACEUI_OK) {
    api.disconnect(me);
    return 0;
  }
  void* wake = nullptr;
  if (!(GRACEUI_API1_COVERS(api.table(), wake_event) && api.has_cap(GRACEUI_CAP_WAKE) &&
        api.table()->wake_event(me, &wake) == GRACEUI_OK)) {
    wake = nullptr;
  }
  Space space(api, me);
  for (;;) {
    if (wake != nullptr) {
      WaitForSingleObject(wake, INFINITE);  // set whenever GraceUI keeps something for this mod's poll
    } else {
      Sleep(10);
    }
    for (;;) {
      GraceUiEvent event{};
      if (api.poll(me, event, g_frame, sizeof g_frame) != GRACEUI_OK || event.kind == GRACEUI_EVENT_NONE) {
        break;
      }
      if (event.kind == GRACEUI_EVENT_WATCHED || event.kind == GRACEUI_EVENT_UNWATCHED ||
          event.kind == GRACEUI_EVENT_COMMAND) {
        space.handle(event);
      }
    }
  }
}

}  // namespace

BOOL WINAPI DllMain(HINSTANCE module, DWORD reason, LPVOID) {
  if (reason == DLL_PROCESS_ATTACH) {
    DisableThreadLibraryCalls(module);
    const HANDLE thread = CreateThread(nullptr, 0, &hello_worker, nullptr, 0, nullptr);  // runs once the loader is done
    if (thread != nullptr) {
      CloseHandle(thread);
    }
  }
  return TRUE;
}
