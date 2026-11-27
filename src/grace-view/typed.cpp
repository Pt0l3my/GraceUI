// SPDX-License-Identifier: MIT
// typed.cpp: a number typed into a slider's field (§3.8, D266). See typed.h.
#include "grace/view/typed.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <limits>

namespace grace::view {

using protocol::Json;
using protocol::Setting;
using protocol::SettingType;

int step_decimals(double step) {
  int d = 0;
  while (d < 4 && std::fabs(step * std::pow(10.0, d) - std::round(step * std::pow(10.0, d))) > 1e-9) {
    ++d;
  }
  return d;
}

int grid_decimals(const Setting& s) {
  const int step = step_decimals(s.step.value_or(1.0));
  return s.min_ ? std::max(step, step_decimals(*s.min_)) : step;
}

std::string plain_number(const Setting& s, double n) {
  char text[64];
  std::snprintf(text, sizeof text, "%.*f", grid_decimals(s), n);
  return text;
}

std::string format_number(const Setting& s, double n) {
  std::string out = plain_number(s, n);
  if (s.unit) {
    const bool word = !s.unit->empty() && (*s.unit)[0] >= 'a' && (*s.unit)[0] <= 'z';
    out += (word ? " " : "") + *s.unit;
  }
  return out;
}

namespace {

std::string_view trimmed(std::string_view text) {
  while (!text.empty() && text.front() == ' ') {
    text.remove_prefix(1);
  }
  while (!text.empty() && text.back() == ' ') {
    text.remove_suffix(1);
  }
  return text;
}

// The bounds in words, as a refusal says them.
std::string bounds_words(const Setting& s) {
  if (s.min_ && s.max_) {
    return "Type a number from " + format_number(s, *s.min_) + " to " + format_number(s, *s.max_) + ".";
  }
  if (s.min_) {
    return "Type a number of " + format_number(s, *s.min_) + " or more.";
  }
  if (s.max_) {
    return "Type a number of " + format_number(s, *s.max_) + " or less.";
  }
  return "Type a number.";
}

// An optional sign, then digits with at most one decimal point (a full stop or a comma) and at least one digit, as the
// C locale's text for from_chars: "-0.5" for "-,5". False for anything else.
bool number_text(std::string_view text, std::string& out) {
  out.clear();
  std::size_t at = 0;
  if (at < text.size() && (text[at] == '+' || text[at] == '-')) {
    if (text[at] == '-') {
      out += '-';
    }
    ++at;
  }
  bool digit = false;
  bool point = false;
  std::string digits;
  for (; at < text.size(); ++at) {
    const char c = text[at];
    if (c >= '0' && c <= '9') {
      digits += c;
      digit = true;
    } else if ((c == '.' || c == ',') && !point) {
      digits += '.';
      point = true;
    } else {
      return false;
    }
  }
  if (!digit) {
    return false;
  }
  if (digits.front() == '.') {
    digits.insert(digits.begin(), '0');
  }
  if (digits.back() == '.') {
    digits.pop_back();
  }
  out += digits;
  return true;
}

}  // namespace

Typed typed_value(const Setting& s, std::string_view text) {
  Typed out;
  text = trimmed(text);
  if (s.unit && !s.unit->empty() && text.size() >= s.unit->size() && text.ends_with(*s.unit)) {
    text = trimmed(text.substr(0, text.size() - s.unit->size()));
  } else if (s.unit && *s.unit == "×" && !text.empty() && (text.back() == 'x' || text.back() == 'X')) {
    text = trimmed(text.substr(0, text.size() - 1));  // "1.3x": the sign typed as the letter
  }
  if (text.empty()) {
    return out;  // nothing typed: nothing sent, nothing said
  }
  std::string number;
  double x = 0;
  if (text.size() > kTypedNumberMax || !number_text(text, number)) {
    out.words = bounds_words(s);
    return out;
  }
  const auto [end, error] = std::from_chars(number.data(), number.data() + number.size(), x);
  if (error != std::errc{} || end != number.data() + number.size() || !std::isfinite(x)) {
    out.words = bounds_words(s);
    return out;
  }
  if ((s.min_ && x < *s.min_) || (s.max_ && x > *s.max_)) {
    out.words = bounds_words(s);
    return out;
  }
  if (std::fabs(x) >= 1e15) {
    out.words = "Type a number of at most 15 digits.";  // beyond exact whole numbers in either view
    return out;
  }
  double n = x;
  if (s.step && *s.step > 0) {
    // The nearest step on the grid the settings store checks (from the minimum, or 0), within the bounds.
    const double step = *s.step;
    const double from = s.min_.value_or(0.0);
    double k = std::round((x - from) / step);
    if (s.max_) {
      k = std::min(k, std::floor((*s.max_ - from) / step + 1e-9));
    }
    if (s.min_) {
      k = std::max(k, 0.0);
    }
    n = from + k * step;
    const double scale = std::pow(10.0, grid_decimals(s));
    n = std::round(n * scale) / scale;
  }
  if (n == 0) {
    n = 0;  // never a negative zero
  }
  out.ok = true;
  if (s.type == SettingType::integer) {
    out.value = Json(static_cast<std::int64_t>(std::llround(n)));
  } else {
    out.value = Json(n);
  }
  return out;
}

}  // namespace grace::view
