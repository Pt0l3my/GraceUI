// SPDX-License-Identifier: MIT
// filter.h: a table's filter bar (G43, D250): which of its rows show, and in what order, from the rows its space
// published. Pure. The browser view's web/src/model/filter.ts does the same, and both run fixtures/filter-cases.json:
// a cell's words as the table draws them; text compared by code point (UTF-8's bytes) with ASCII letters folded; two
// numbers as numbers; a row with no value for the sort key last in either direction; equal rows in the space's order.
#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "grace/protocol/codec.h"
#include "grace/protocol/messages.h"

namespace grace::view {

// The player's choices on one bar, for the session: each filter's value (none for All), the sort option, whether its
// direction is reversed, and the search's words.
struct FilterState {
  std::vector<std::optional<std::string>> chosen;
  std::size_t sort = 0;
  bool reversed = false;
  std::string search;
  bool operator==(const FilterState&) const = default;
};

// A cell's words, as the table draws them: a string as it is, a number as JSON writes it, anything else nothing.
std::string filter_words(const protocol::Json& row, std::string_view column);

// ASCII letters to lower case; every other byte as it is.
std::string fold(std::string_view text);

// A filter's choices: the column's values the rows hold, each once, in the order the rows give them (invariant 2). An
// empty cell is no choice.
std::vector<std::string> filter_choices(const protocol::Json& rows, std::string_view column);

// Whether any filter or the search holds: the count line shows only then (a sort alone hides nothing).
bool narrowed(const protocol::FilterBar& bar, const FilterState& state);

// Whether the sort shows its rows descending, its option's own direction reversed or not.
bool descending(const protocol::FilterBar& bar, const FilterState& state);

// The rows that show, as indices into `rows` (a JSON array), in the order they show.
std::vector<std::size_t> visible_rows(const protocol::Json& rows, const protocol::FilterBar& bar,
                                      const FilterState& state);

}  // namespace grace::view
