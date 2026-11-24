// SPDX-License-Identifier: MIT
// filter.cpp: a table's filter bar (filter.h).
#include "grace/view/filter.h"

#include <algorithm>
#include <set>
#include <variant>

namespace grace::view {
namespace {

using protocol::Json;

// A sort key's value: a number, its words, or none (absent, empty or neither).
using Key = std::variant<std::monostate, double, std::string>;

Key key_of(const Json& row, const std::string& key) {
  const auto found = row.find(key);
  if (found == row.end()) {
    return {};
  }
  if (found->is_number()) {
    return found->get<double>();
  }
  if (found->is_string() && !found->get_ref<const std::string&>().empty()) {
    return found->get<std::string>();
  }
  return {};
}

std::string words_of(const Key& k, const Json& row, const std::string& key) {
  if (const std::string* s = std::get_if<std::string>(&k)) {
    return *s;
  }
  return row.find(key)->dump();  // a number as JSON writes it, as the browser's JSON.stringify does
}

}  // namespace

std::string filter_words(const Json& row, std::string_view column) {
  if (!row.is_object()) {
    return {};
  }
  const auto found = row.find(std::string(column));
  if (found == row.end()) {
    return {};
  }
  if (found->is_string()) {
    return found->get<std::string>();
  }
  return found->is_number() ? found->dump() : std::string();
}

std::string fold(std::string_view text) {
  std::string out(text);
  for (char& c : out) {
    if (c >= 'A' && c <= 'Z') {
      c = static_cast<char>(c - 'A' + 'a');
    }
  }
  return out;
}

std::vector<std::string> filter_choices(const Json& rows, std::string_view column) {
  std::vector<std::string> out;
  if (!rows.is_array()) {
    return out;
  }
  std::set<std::string> seen;
  for (const Json& row : rows) {
    std::string w = filter_words(row, column);
    if (!w.empty() && seen.insert(w).second) {
      out.push_back(std::move(w));
    }
  }
  return out;
}

bool narrowed(const protocol::FilterBar& bar, const FilterState& state) {
  const std::size_t filters = bar.filters ? bar.filters->size() : 0;
  for (std::size_t i = 0; i < filters && i < state.chosen.size(); ++i) {
    if (state.chosen[i]) {
      return true;
    }
  }
  if (!bar.search) {
    return false;
  }
  return state.search.find_first_not_of(" \t\r\n") != std::string::npos;
}

bool descending(const protocol::FilterBar& bar, const FilterState& state) {
  if (!bar.sort || state.sort >= bar.sort->size()) {
    return false;
  }
  const protocol::SortOption& option = (*bar.sort)[state.sort];
  const bool own = option.direction && *option.direction == protocol::SortDirection::descending;
  return own != state.reversed;
}

std::vector<std::size_t> visible_rows(const Json& rows, const protocol::FilterBar& bar, const FilterState& state) {
  std::vector<std::size_t> out;
  if (!rows.is_array()) {
    return out;
  }
  // The search's words, trimmed as the browser's trim() does for these, and folded.
  std::string needle;
  if (bar.search) {
    const std::size_t from = state.search.find_first_not_of(" \t\r\n");
    const std::size_t to = state.search.find_last_not_of(" \t\r\n");
    needle = from == std::string::npos ? std::string() : fold(std::string_view(state.search).substr(from, to - from + 1));
  }
  for (std::size_t index = 0; index < rows.size(); ++index) {
    const Json& row = rows[index];
    if (!row.is_object()) {
      continue;
    }
    bool kept = true;
    if (bar.filters) {
      for (std::size_t i = 0; i < bar.filters->size() && kept; ++i) {
        if (i < state.chosen.size() && state.chosen[i]) {
          kept = filter_words(row, (*bar.filters)[i].column) == *state.chosen[i];
        }
      }
    }
    if (kept && !needle.empty()) {
      kept = std::any_of(bar.search->columns.begin(), bar.search->columns.end(), [&](const std::string& c) {
        return fold(filter_words(row, c)).find(needle) != std::string::npos;
      });
    }
    if (kept) {
      out.push_back(index);
    }
  }
  if (!bar.sort || state.sort >= bar.sort->size()) {
    return out;
  }
  const std::string& key = (*bar.sort)[state.sort].key;
  const bool down = descending(bar, state);
  std::vector<Key> keys(rows.size());
  for (const std::size_t i : out) {
    keys[i] = key_of(rows[i], key);
  }
  std::stable_sort(out.begin(), out.end(), [&](std::size_t i, std::size_t j) {
    const Key& a = keys[i];
    const Key& b = keys[j];
    const bool none_a = std::holds_alternative<std::monostate>(a);
    const bool none_b = std::holds_alternative<std::monostate>(b);
    if (none_a || none_b) {
      return !none_a && none_b;  // no value: last, whichever the direction
    }
    int c = 0;
    if (std::holds_alternative<double>(a) && std::holds_alternative<double>(b)) {
      const double x = std::get<double>(a);
      const double y = std::get<double>(b);
      c = x < y ? -1 : (x > y ? 1 : 0);
    } else {
      const std::string x = fold(words_of(a, rows[i], key));
      const std::string y = fold(words_of(b, rows[j], key));
      c = x.compare(y);  // bytes: UTF-8's order is the code points'
      c = c < 0 ? -1 : (c > 0 ? 1 : 0);
    }
    return down ? c > 0 : c < 0;
  });
  return out;
}

}  // namespace grace::view
