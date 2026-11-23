// SPDX-License-Identifier: MIT
// editor.h: the editor block's model (D217, G38, proposal 0010 §3.3 and §7): the document a space publishes on the
// block's topic, read tolerantly (a malformed field is left out, never guessed at), and the player's unsaved edits over
// it, which the view keeps in memory for the session, never on disk (G38 (2)). Pure: no ImGui, no Win32.
//
// The document: {"target", "rev", "state": {word, tone, line}, "draft_blocked", "draft_help", "fields": [...]}. A field:
// {id (dotted ids allowed), label, kind, help, value, reference, reference_label, max_words, max_chars, max_items, min,
// max, choices: [{value, label}], choices_from, locked (words), nullable}. Its kinds: line, prose (one paragraph), list
// (items of words, or {text, choice, key, locked}), choice, pick (choices from a topic), number, switch, shown (read
// only, never sent). `null` as a value is "not set here", which is not empty (EldenNet's answer (c)).
#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "grace/protocol/messages.h"

namespace grace::view {

enum class FieldKind : std::uint8_t { line, prose, list, choice, pick, number, toggle, shown };

struct EditorField {
  std::string id;
  std::string label;
  FieldKind kind = FieldKind::line;
  std::string help;
  protocol::Json value;      // as published: a string, a list, a number, a bool, or null ("not set here")
  protocol::Json reference;  // read-only beside the field, never sent: a string or a list of strings, or null
  std::string reference_label;
  std::size_t max_words = 0;  // 0: none
  std::size_t max_chars = 0;
  std::size_t max_items = 0;
  std::optional<double> min;
  std::optional<double> max;
  std::vector<protocol::Choice> choices;  // a choice's, or a list item's choice
  std::string choices_from;               // a pick's, or a list item's choice: a state topic's {"choices": [...]}
  std::string locked;                     // words: shown, never edited, sent back as published
  bool nullable = false;                  // may be left "not set here"
};

struct EditorDocument {
  std::string target;
  std::uint64_t rev = 0;
  std::string state_word;
  std::string state_tone;  // moss, ember, glint or rot
  std::string state_line;
  std::string draft_blocked;  // why Draft is not available now, in the space's words
  std::string draft_help;     // what a draft sends, and to whom, in the space's words
  std::vector<EditorField> fields;

  // The document of a topic's body; false if it is none (no fields array, or no target).
  static bool parse(const protocol::Json& body, EditorDocument& out);
  const EditorField* field(std::string_view id) const;
};

// A message of a check or a save: about a field (and an item of a list), or about the whole file (no field).
struct EditorMessage {
  std::string field;
  std::optional<std::size_t> item;
  bool error = false;  // an error blocks the save; a warning does not
  std::string words;
};

// Words (runs of characters that are not spaces) and characters (code points) of UTF-8 text.
std::size_t count_words(std::string_view text) noexcept;
std::size_t count_chars(std::string_view text) noexcept;

// The player's edits over one document, by its topic (a family's member), kept while the session lasts.
struct EditorState {
  std::uint64_t base_rev = 0;  // the document's revision when the first unsaved edit was made
  std::map<std::string, protocol::Json, std::less<>> edits;  // a field's value as the player left it
  std::vector<EditorMessage> messages;                       // the last check's or save's
  std::string top;                                           // the last answer's words for the whole block
  std::map<std::string, protocol::Json, std::less<>> draft;  // a draft's proposal per field, unsaved
  std::string notice;                                        // a draft's notice (one discarded for safety)
  std::string notes;                                         // the draft's notes, as typed
  bool conflict = false;                                     // the last save found the document changed

  bool dirty() const noexcept { return !edits.empty(); }
  // A field's value as the view shows it: the player's edit, or as published.
  const protocol::Json& value(const EditorField& field) const;
  // Sets a field's value: an edit, or none if it is the published value again.
  void set(const EditorField& field, protocol::Json value);
  // The document as check and save send it: {id: value} for every field but a shown one.
  protocol::Json doc(const EditorDocument& document) const;
  // An answer's body: its messages ({field?, item?, level, words}), its draft ({draft: {id: value}, notice}).
  void take_messages(const protocol::Json& body);
  void take_draft(const EditorDocument& document, const protocol::Json& body);
  // A draft's proposal used for a field (an edit), or let go.
  void use(const EditorField& field);
  void keep(std::string_view field);
  // Everything let go, as after a save, a reset or a reload.
  void clear();
};

// A list item's parts: its text, its choice, its key and its locked words (a string item has its text alone).
struct ListItem {
  std::string text;
  std::string choice;
  protocol::Json key;  // the space's opaque key, sent back with it; null for one the player added
  std::string locked;
};
ListItem list_item(const protocol::Json& item);
protocol::Json list_json(const ListItem& item, bool with_choice);

}  // namespace grace::view
