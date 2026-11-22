// keys.cpp: keys' names (keys.h, D206).
#include "grace/input/keys.h"

#include <array>
#include <utility>

namespace grace::input {
namespace {

struct Named {
  Key key;
  std::string_view name;
};

// DirectInput's codes (DIK_*), the key's place on a US layout.
constexpr std::array<Named, 104> kNames = {{
    {0x01, "Escape"},      {0x02, "1"},           {0x03, "2"},           {0x04, "3"},
    {0x05, "4"},           {0x06, "5"},           {0x07, "6"},           {0x08, "7"},
    {0x09, "8"},           {0x0A, "9"},           {0x0B, "0"},           {0x0C, "Minus"},
    {0x0D, "Equal"},       {0x0E, "Backspace"},   {0x0F, "Tab"},         {0x10, "Q"},
    {0x11, "W"},           {0x12, "E"},           {0x13, "R"},           {0x14, "T"},
    {0x15, "Y"},           {0x16, "U"},           {0x17, "I"},           {0x18, "O"},
    {0x19, "P"},           {0x1A, "BracketLeft"}, {0x1B, "BracketRight"}, {0x1C, "Enter"},
    {0x1D, "ControlLeft"}, {0x1E, "A"},           {0x1F, "S"},           {0x20, "D"},
    {0x21, "F"},           {0x22, "G"},           {0x23, "H"},           {0x24, "J"},
    {0x25, "K"},           {0x26, "L"},           {0x27, "Semicolon"},   {0x28, "Quote"},
    {0x29, "Backquote"},   {0x2A, "ShiftLeft"},   {0x2B, "Backslash"},   {0x2C, "Z"},
    {0x2D, "X"},           {0x2E, "C"},           {0x2F, "V"},           {0x30, "B"},
    {0x31, "N"},           {0x32, "M"},           {0x33, "Comma"},       {0x34, "Period"},
    {0x35, "Slash"},       {0x36, "ShiftRight"},  {0x37, "NumpadStar"},  {0x38, "AltLeft"},
    {0x39, "Space"},       {0x3A, "CapsLock"},    {0x3B, "F1"},          {0x3C, "F2"},
    {0x3D, "F3"},          {0x3E, "F4"},          {0x3F, "F5"},          {0x40, "F6"},
    {0x41, "F7"},          {0x42, "F8"},          {0x43, "F9"},          {0x44, "F10"},
    {0x45, "NumLock"},     {0x46, "ScrollLock"},  {0x47, "Numpad7"},     {0x48, "Numpad8"},
    {0x49, "Numpad9"},     {0x4A, "NumpadMinus"}, {0x4B, "Numpad4"},     {0x4C, "Numpad5"},
    {0x4D, "Numpad6"},     {0x4E, "NumpadPlus"},  {0x4F, "Numpad1"},     {0x50, "Numpad2"},
    {0x51, "Numpad3"},     {0x52, "Numpad0"},     {0x53, "NumpadDot"},   {0x56, "Oem102"},
    {0x57, "F11"},         {0x58, "F12"},         {0x64, "F13"},         {0x65, "F14"},
    {0x66, "F15"},         {0x9C, "NumpadEnter"}, {0x9D, "ControlRight"}, {0xB5, "NumpadSlash"},
    {0xB7, "PrintScreen"}, {0xB8, "AltRight"},    {0xC5, "Pause"},       {0xC7, "Home"},
    {0xC8, "ArrowUp"},     {0xC9, "PageUp"},      {0xCB, "ArrowLeft"},   {0xCD, "ArrowRight"},
    {0xCF, "End"},         {0xD0, "ArrowDown"},   {0xD1, "PageDown"},    {0xD2, "Insert"},
}};

// The rest of DIK's named keys that a hotkey may take, past the table's tidy rows.
constexpr std::array<Named, 3> kMore = {{{0xD3, "Delete"}, {0xDB, "MetaLeft"}, {0xDD, "ContextMenu"}}};

}  // namespace

std::string_view key_name(Key key) noexcept {
  for (const Named& named : kNames) {
    if (named.key == key) {
      return named.name;
    }
  }
  for (const Named& named : kMore) {
    if (named.key == key) {
      return named.name;
    }
  }
  return {};
}

bool key_from_text(std::string_view text, Key& key, std::uint8_t& modifiers) noexcept {
  key = kNoKey;
  modifiers = 0;
  if (text.empty()) {
    return true;
  }
  for (const auto& [prefix, bit] : {std::pair<std::string_view, std::uint8_t>{"Ctrl+", 1u}, {"Shift+", 2u}, {"Alt+", 4u}}) {
    if (text.substr(0, prefix.size()) == prefix) {
      text.remove_prefix(prefix.size());
      modifiers = static_cast<std::uint8_t>(modifiers | bit);
    }
  }
  for (const Named& named : kNames) {
    if (named.key != kNoKey && named.name == text) {
      key = named.key;
      return true;
    }
  }
  for (const Named& named : kMore) {
    if (named.name == text) {
      key = named.key;
      return true;
    }
  }
  modifiers = 0;
  return false;
}

std::string key_text(Key key, std::uint8_t modifiers) {
  std::string text;
  if ((modifiers & 1u) != 0) {
    text += "Ctrl+";
  }
  if ((modifiers & 2u) != 0) {
    text += "Shift+";
  }
  if ((modifiers & 4u) != 0) {
    text += "Alt+";
  }
  text += key_name(key);
  return text;
}

}  // namespace grace::input
