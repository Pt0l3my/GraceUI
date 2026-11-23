// keys.h: one name for a key across the three ways GraceUI sees the keyboard (D124): DirectInput's key code (DIK_*),
// which is the key's scan code with 0x80 added for an extended key, and which a window message's scan code and
// extended bit give too. Pure: no Win32 header and no Dear ImGui, so the rules are tested on Linux.
#pragma once

#include <array>
#include <atomic>
#include <cstdint>
#include <string>
#include <string_view>

namespace grace::input {

using Key = std::uint8_t;

inline constexpr Key kNoKey = 0;      // a message no physical key made: an IME's character, or one a program posted
inline constexpr Key kEscape = 0x01;  // DIK_ESCAPE
inline constexpr Key kF5 = 0x3F;      // DIK_F5: the menu key's default (§5.1)

// The few virtual-key codes these rules need, as plain numbers, so that no Windows header is needed.
inline constexpr std::uint32_t kVkPause = 0x13;
inline constexpr std::uint32_t kVkF4 = 0x73;
inline constexpr std::uint32_t kVkNumLock = 0x90;

// The key of a keyboard message (WM_KEYDOWN, WM_KEYUP, WM_CHAR and their system forms): bits 16 to 23 of its lParam
// are the scan code, bit 24 the extended flag. Windows marks Num Lock extended and Pause not, the other way round from
// DirectInput's codes, so those two go by their virtual key (0 for a character message).
constexpr Key key_from_message(std::uint32_t lparam, std::uint32_t vk) noexcept {
  if (vk == kVkNumLock) {
    return 0x45;  // DIK_NUMLOCK
  }
  if (vk == kVkPause) {
    return 0xC5;  // DIK_PAUSE
  }
  const std::uint32_t scan = (lparam >> 16) & 0x7Fu;
  if (scan == 0) {
    return kNoKey;
  }
  return static_cast<Key>(scan | (((lparam >> 24) & 1u) != 0 ? 0x80u : 0u));
}

// A key's name as GraceUI shows and stores it (D206): 1 to 12 of A-Z, a-z and 0-9, after the browser's
// KeyboardEvent.code without its "Key" and "Digit" prefixes (A, 7, F5, PageUp, BracketLeft, NumpadEnter), for the key at
// that place on a US layout, whatever the player's own layout prints on it. Empty for a code with no name.
std::string_view key_name(Key key) noexcept;

// A key with its modifiers (GRACEUI_MOD_*: Ctrl 1, Shift 2, Alt 4) as the settings' key names write it: "Ctrl+Shift+K".
std::string key_text(Key key, std::uint8_t modifiers);
// The other way: false for a text that is not a key's name with its modifiers (key_text's form; "" is no key).
bool key_from_text(std::string_view text, Key& key, std::uint8_t& modifiers) noexcept;

// A set of keys, by their code.
class KeyBits {
 public:
  constexpr KeyBits() = default;

  static constexpr KeyBits all() noexcept {
    KeyBits bits;
    for (std::uint64_t& word : bits.words_) {
      word = ~std::uint64_t{0};
    }
    return bits;
  }

  constexpr void set(Key key) noexcept { words_[key >> 6] |= bit(key); }
  constexpr void clear(Key key) noexcept { words_[key >> 6] &= ~bit(key); }
  constexpr bool test(Key key) const noexcept { return (words_[key >> 6] & bit(key)) != 0; }
  constexpr void clear_all() noexcept { words_ = {}; }

  constexpr bool any() const noexcept {
    for (const std::uint64_t word : words_) {
      if (word != 0) {
        return true;
      }
    }
    return false;
  }

  constexpr int count() const noexcept {
    int total = 0;
    for (std::uint64_t word : words_) {
      for (; word != 0; word &= word - 1) {
        ++total;
      }
    }
    return total;
  }

  constexpr KeyBits operator|(const KeyBits& other) const noexcept {
    KeyBits out;
    for (int i = 0; i < 4; ++i) {
      out.words_[i] = words_[i] | other.words_[i];
    }
    return out;
  }

  constexpr KeyBits operator&(const KeyBits& other) const noexcept {
    KeyBits out;
    for (int i = 0; i < 4; ++i) {
      out.words_[i] = words_[i] & other.words_[i];
    }
    return out;
  }

  constexpr bool operator==(const KeyBits&) const noexcept = default;

  constexpr std::uint64_t word(int index) const noexcept { return words_[index]; }
  constexpr void set_word(int index, std::uint64_t value) noexcept { words_[index] = value; }

 private:
  static constexpr std::uint64_t bit(Key key) noexcept { return std::uint64_t{1} << (key & 63u); }

  std::array<std::uint64_t, 4> words_{};
};

// A set of keys that any thread may change and read without waiting; each operation is atomic word by word.
class AtomicKeyBits {
 public:
  constexpr AtomicKeyBits() = default;
  AtomicKeyBits(const AtomicKeyBits&) = delete;
  AtomicKeyBits& operator=(const AtomicKeyBits&) = delete;

  KeyBits load() const noexcept {
    KeyBits out;
    for (int i = 0; i < 4; ++i) {
      out.set_word(i, words_[i].load(std::memory_order_acquire));
    }
    return out;
  }

  void store(const KeyBits& bits) noexcept {
    for (int i = 0; i < 4; ++i) {
      words_[i].store(bits.word(i), std::memory_order_release);
    }
  }

  void or_with(const KeyBits& bits) noexcept {
    for (int i = 0; i < 4; ++i) {
      words_[i].fetch_or(bits.word(i), std::memory_order_acq_rel);
    }
  }

  void and_with(const KeyBits& bits) noexcept {
    for (int i = 0; i < 4; ++i) {
      words_[i].fetch_and(bits.word(i), std::memory_order_acq_rel);
    }
  }

 private:
  std::array<std::atomic<std::uint64_t>, 4> words_{};
};

}  // namespace grace::input
