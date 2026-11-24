// SPDX-License-Identifier: MIT
// scenario.h: grace-dev's scripted time (§9.2, D168): one space's timeline of steps at given milliseconds (replace a
// state topic's body, append rows to a stream, clear a stream), its reactions to the space's own commands, and what each
// inherit tri-state on inherit resolves to (D163). The format is in tools/grace-dev/README.md. A scenario is checked
// whole against its space before anything plays; a failure names the file, the line and the step.
#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "grace/protocol/messages.h"
#include "space_data.h"

namespace grace::dev {

inline constexpr std::uint32_t kScenarioFormat = 1;

struct Action {
  enum class Kind : std::uint8_t {
    state,  // replace `topic`'s body with `body`
    rows,   // append `rows` to the stream `topic`, numbered by grace-dev
    clear,  // empty the stream `topic`: its next rows start it over, after a gap (§6.3, D161)
  };
  Kind kind = Kind::state;
  std::string topic;
  protocol::Json body;
  std::vector<protocol::Json> rows;
};

struct Step {
  std::uint64_t at = 0;  // milliseconds from the start of a pass, at speed 1
  Action action;
  std::size_t line = 0;
};

struct Reaction {
  std::string command;
  std::vector<Action> actions;
  bool ok = true;  // the answer: ok, or `error` with `detail` in the result's body
  protocol::ErrorCode error = protocol::ErrorCode::internal;
  std::string detail;
  std::optional<protocol::Json> body;  // an ok answer's body: a page of rows for a page command (D254)
  std::size_t line = 0;
};

struct Scenario {
  std::string name;   // its file, for messages
  std::string space;  // the space it plays in
  std::uint64_t length = 0;  // a pass's length in ms: its own length_ms, or its last step's time
  bool loop = false;
  std::vector<Step> steps;  // in time order
  std::vector<Reaction> reactions;
  std::vector<protocol::InheritedValue> inherited;

  const Reaction* reaction(std::string_view command) const noexcept;
};

// The scenario in `text` (its file `name`), read and checked against `space`: false with `why` naming the file, the
// line and the step or reaction that fails ("scenario.json line 14: step 3: ...").
bool load_scenario(std::string_view text, std::string_view name, const SpaceData& space, Scenario& out,
                   std::string& why);

// The space a scenario names, read before its space is known (for --scenario FILE): false with `why`.
bool scenario_space(std::string_view text, std::string_view name, std::string& space, std::string& why);

}  // namespace grace::dev
