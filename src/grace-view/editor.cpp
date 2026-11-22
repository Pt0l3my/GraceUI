// SPDX-License-Identifier: MIT
// editor.cpp: the editor block's model (editor.h; D217).
#include "grace/view/editor.h"

#include <algorithm>

namespace grace::view {
namespace {

using protocol::Json;

std::string text_of(const Json& object, const char* name, std::size_t max = 1024) {
  const auto found = object.find(name);
  if (found == object.end() || !found->is_string()) {
    return {};
  }
  std::string text = found->get<std::string>();
  return text.size() <= max ? text : std::string();
}

std::size_t size_of(const Json& object, const char* name) {
  const auto found = object.find(name);
  if (found == object.end() || !found->is_number_unsigned()) {
    return 0;
  }
  return static_cast<std::size_t>(std::min<std::uint64_t>(found->get<std::uint64_t>(), 100000));
}

std::optional<FieldKind> kind_of(std::string_view kind) {
  if (kind == "line") return FieldKind::line;
  if (kind == "prose") return FieldKind::prose;
  if (kind == "list") return FieldKind::list;
  if (kind == "choice") return FieldKind::choice;
  if (kind == "pick") return FieldKind::pick;
  if (kind == "number") return FieldKind::number;
  if (kind == "switch") return FieldKind::toggle;
  if (kind == "shown") return FieldKind::shown;
  return std::nullopt;
}

// Whether a value suits its field's kind (null only where the field may be left unset).
bool fits(const EditorField& field, const Json& value) {
  if (value.is_null()) {
    return field.nullable || field.kind == FieldKind::shown;
  }
  switch (field.kind) {
    case FieldKind::line:
    case FieldKind::prose:
    case FieldKind::choice:
    case FieldKind::pick:
    case FieldKind::shown:
      return value.is_string();
    case FieldKind::list:
      return value.is_array() && std::all_of(value.begin(), value.end(), [](const Json& item) {
               return item.is_string() || (item.is_object() && item.contains("text") && item["text"].is_string());
             });
    case FieldKind::number:
      return value.is_number();
    case FieldKind::toggle:
      return value.is_boolean();
  }
  return false;
}

}  // namespace

bool EditorDocument::parse(const Json& body, EditorDocument& out) {
  out = EditorDocument{};
  if (!body.is_object() || !body.contains("fields") || !body["fields"].is_array()) {
    return false;
  }
  out.target = text_of(body, "target", 256);
  if (out.target.empty()) {
    return false;
  }
  if (body.contains("rev") && body["rev"].is_number_unsigned()) {
    out.rev = body["rev"].get<std::uint64_t>();
  }
  if (body.contains("state") && body["state"].is_object()) {
    out.state_word = text_of(body["state"], "word", 64);
    out.state_tone = text_of(body["state"], "tone", 16);
    out.state_line = text_of(body["state"], "line", 256);
  }
  out.draft_blocked = text_of(body, "draft_blocked", 256);
  out.draft_help = text_of(body, "draft_help", 512);
  for (const Json& item : body["fields"]) {
    if (!item.is_object() || out.fields.size() >= 64) {
      continue;
    }
    EditorField field;
    field.id = text_of(item, "id", 64);
    field.label = text_of(item, "label", 128);
    const std::optional<FieldKind> kind = kind_of(text_of(item, "kind", 16));
    if (field.id.empty() || field.label.empty() || !kind || out.field(field.id) != nullptr) {
      continue;  // a field with no id, label or known kind, or a second of one id: left out, never guessed at
    }
    field.kind = *kind;
    field.help = text_of(item, "help", 512);
    field.reference_label = text_of(item, "reference_label", 64);
    field.max_words = size_of(item, "max_words");
    field.max_chars = size_of(item, "max_chars");
    field.max_items = size_of(item, "max_items");
    if (item.contains("min") && item["min"].is_number()) field.min = item["min"].get<double>();
    if (item.contains("max") && item["max"].is_number()) field.max = item["max"].get<double>();
    field.choices_from = text_of(item, "choices_from", 64);
    field.locked = text_of(item, "locked", 256);
    field.nullable = item.contains("nullable") && item["nullable"].is_boolean() && item["nullable"].get<bool>();
    if (item.contains("choices") && item["choices"].is_array()) {
      for (const Json& choice : item["choices"]) {
        if (choice.is_object() && !text_of(choice, "value", 200).empty()) {
          field.choices.push_back(protocol::Choice{text_of(choice, "value", 200), text_of(choice, "label", 128)});
        }
      }
    }
    if (item.contains("reference") &&
        (item["reference"].is_string() || (item["reference"].is_array() &&
                                           std::all_of(item["reference"].begin(), item["reference"].end(),
                                                       [](const Json& r) { return r.is_string(); })))) {
      field.reference = item["reference"];
    }
    field.value = item.contains("value") ? item["value"] : Json();
    if (!fits(field, field.value)) {
      continue;  // a value of another kind's: the field is left out
    }
    out.fields.push_back(std::move(field));
  }
  return true;
}

const EditorField* EditorDocument::field(std::string_view id) const {
  for (const EditorField& f : fields) {
    if (f.id == id) {
      return &f;
    }
  }
  return nullptr;
}

std::size_t count_words(std::string_view text) noexcept {
  std::size_t count = 0;
  bool in_word = false;
  for (const char c : text) {
    const bool space = c == ' ';
    if (!space && !in_word) {
      ++count;
    }
    in_word = !space;
  }
  return count;
}

std::size_t count_chars(std::string_view text) noexcept {
  return static_cast<std::size_t>(std::count_if(text.begin(), text.end(), [](char b) {
    return (static_cast<unsigned char>(b) & 0xC0u) != 0x80u;
  }));
}

const Json& EditorState::value(const EditorField& field) const {
  const auto found = edits.find(field.id);
  return found != edits.end() ? found->second : field.value;
}

void EditorState::set(const EditorField& field, Json value) {
  if (!field.locked.empty() || field.kind == FieldKind::shown) {
    return;
  }
  if (value == field.value) {
    edits.erase(field.id);  // as published again: no edit
  } else {
    edits[field.id] = std::move(value);
  }
}

Json EditorState::doc(const EditorDocument& document) const {
  Json out = Json::object();
  for (const EditorField& field : document.fields) {
    if (field.kind != FieldKind::shown) {
      out[field.id] = value(field);  // a locked field and a locked item go back as published
    }
  }
  return out;
}

void EditorState::take_messages(const Json& body) {
  messages.clear();
  if (!body.is_object() || !body.contains("messages") || !body["messages"].is_array()) {
    return;
  }
  for (const Json& item : body["messages"]) {
    if (!item.is_object() || messages.size() >= 64) {
      continue;
    }
    EditorMessage message;
    message.words = text_of(item, "words", 256);
    if (message.words.empty()) {
      continue;
    }
    message.field = text_of(item, "field", 64);
    if (item.contains("item") && item["item"].is_number_unsigned()) {
      message.item = static_cast<std::size_t>(item["item"].get<std::uint64_t>());
    }
    message.error = text_of(item, "level", 16) != "warning";
    messages.push_back(std::move(message));
  }
}

void EditorState::take_draft(const EditorDocument& document, const Json& body) {
  draft.clear();
  notice = body.is_object() ? text_of(body, "notice", 256) : std::string();
  if (!body.is_object() || !body.contains("draft") || !body["draft"].is_object()) {
    return;
  }
  for (const auto& [id, value] : body["draft"].items()) {
    const EditorField* field = document.field(id);
    if (field != nullptr && field->kind != FieldKind::shown && field->locked.empty() && !value.is_null() &&
        fits(*field, value)) {
      draft[id] = value;  // only fields the document has, of their own kinds
    }
  }
}

void EditorState::use(const EditorField& field) {
  const auto found = draft.find(field.id);
  if (found == draft.end()) {
    return;
  }
  set(field, found->second);
  draft.erase(found);
}

void EditorState::keep(std::string_view field) {
  if (const auto found = draft.find(field); found != draft.end()) {
    draft.erase(found);
  }
}

void EditorState::clear() {
  edits.clear();
  messages.clear();
  top.clear();
  draft.clear();
  notice.clear();
  conflict = false;
  base_rev = 0;
}

ListItem list_item(const Json& item) {
  ListItem out;
  if (item.is_string()) {
    out.text = item.get<std::string>();
    return out;
  }
  if (item.is_object()) {
    out.text = text_of(item, "text", 4096);
    out.choice = text_of(item, "choice", 200);
    out.locked = text_of(item, "locked", 256);
    if (item.contains("key")) {
      out.key = item["key"];
    }
  }
  return out;
}

Json list_json(const ListItem& item, bool with_choice) {
  if (!with_choice && item.key.is_null() && item.locked.empty()) {
    return Json(item.text);  // a plain item stays a string
  }
  Json out = Json::object({{"text", item.text}});
  if (with_choice) {
    out["choice"] = item.choice;
  }
  if (!item.key.is_null()) {
    out["key"] = item.key;
  }
  if (!item.locked.empty()) {
    out["locked"] = item.locked;
  }
  return out;
}

}  // namespace grace::view
