// SPDX-License-Identifier: MIT
// document.cpp: a document block's body (document.h).
#include "grace/view/document.h"

#include <array>
#include <optional>

namespace grace::view {
namespace {

using protocol::Json;

// Characters as code points: UTF-8's bytes that start one.
std::size_t chars(const std::string& text) {
  std::size_t n = 0;
  for (const char c : text) {
    n += (static_cast<unsigned char>(c) & 0xC0) != 0x80 ? 1 : 0;
  }
  return n;
}

bool doc_id(const std::string& id) {
  if (id.empty() || id.size() > 64) {
    return false;
  }
  for (const char c : id) {
    if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '.' || c == '_' || c == '-')) {
      return false;
    }
  }
  return true;
}

bool flag(const Json& o, const char* name) {
  const auto found = o.find(name);
  return found != o.end() && found->is_boolean() && found->get<bool>();
}

std::optional<std::vector<DocSpan>> spans_of(const Json& v) {
  Json list = Json::array();
  if (v.is_string()) {
    list.push_back(Json::object({{"text", v}}));
  } else if (v.is_array()) {
    list = v;
  }
  std::vector<DocSpan> out;
  std::size_t total = 0;
  for (const Json& s : list) {
    if (!s.is_object() || !s.contains("text") || !s["text"].is_string() || s["text"].get_ref<const std::string&>().empty()) {
      continue;
    }
    const bool strong = flag(s, "strong");
    const bool code = flag(s, "code");
    const bool link = s.contains("doc") && s["doc"].is_string();
    if (static_cast<int>(strong) + static_cast<int>(code) + static_cast<int>(link) > 1) {
      continue;
    }
    if (link && !doc_id(s["doc"].get<std::string>())) {
      continue;
    }
    DocSpan span;
    span.text = s["text"].get<std::string>();
    span.style = strong ? DocSpan::Style::strong : code ? DocSpan::Style::code : link ? DocSpan::Style::link
                                                                                     : DocSpan::Style::plain;
    if (link) {
      span.doc = s["doc"].get<std::string>();
      if (s.contains("heading") && s["heading"].is_string()) {
        const std::string& h = s["heading"].get_ref<const std::string&>();
        if (!h.empty() && chars(h) <= kDocMaxHeading) {
          span.heading = h;
        }
      }
    }
    total += chars(span.text);
    out.push_back(std::move(span));
  }
  if (out.empty() || total > kDocMaxText) {
    return std::nullopt;
  }
  return out;
}

std::string cell_words(const Json& row, const std::string& column) {
  const auto found = row.find(column);
  if (found == row.end()) {
    return {};
  }
  return found->is_string() ? found->get<std::string>() : found->is_number() ? found->dump() : std::string();
}

std::optional<DocBlock> block_of(const Json& b) {
  if (!b.is_object()) {
    return std::nullopt;
  }
  static constexpr std::array<const char*, 6> kKinds = {"heading", "paragraph", "list", "note", "code", "table"};
  int found = -1;
  for (std::size_t k = 0; k < kKinds.size(); ++k) {
    if (b.contains(kKinds[k])) {
      if (found >= 0) {
        return std::nullopt;  // two kinds at once
      }
      found = static_cast<int>(k);
    }
  }
  DocBlock out;
  switch (found) {
    case 0: {
      const Json& h = b["heading"];
      const Json level = b.contains("level") ? b["level"] : Json(2);
      if (!h.is_string() || h.get_ref<const std::string&>().empty() || chars(h.get<std::string>()) > kDocMaxHeading ||
          !level.is_number_integer() || (level.get<int>() != 2 && level.get<int>() != 3)) {
        return std::nullopt;
      }
      out.kind = DocBlock::Kind::heading;
      out.level = level.get<int>();
      out.text = h.get<std::string>();
      return out;
    }
    case 1:
    case 3: {
      auto spans = spans_of(b[kKinds[static_cast<std::size_t>(found)]]);
      if (!spans) {
        return std::nullopt;
      }
      out.kind = found == 1 ? DocBlock::Kind::paragraph : DocBlock::Kind::note;
      out.spans = std::move(*spans);
      return out;
    }
    case 2: {
      const Json& list = b["list"];
      if (!list.is_array() || list.empty() || list.size() > kDocMaxItems ||
          (b.contains("ordered") && !b["ordered"].is_boolean())) {
        return std::nullopt;
      }
      out.kind = DocBlock::Kind::list;
      out.ordered = flag(b, "ordered");
      for (const Json& item : list) {
        auto spans = spans_of(item);
        if (!spans) {
          return std::nullopt;
        }
        out.items.push_back(std::move(*spans));
      }
      return out;
    }
    case 4: {
      const Json& code = b["code"];
      if (!code.is_string() || code.get_ref<const std::string&>().empty() ||
          code.get_ref<const std::string&>().size() > kDocMaxCode) {
        return std::nullopt;
      }
      out.kind = DocBlock::Kind::code;
      out.text = code.get<std::string>();
      return out;
    }
    case 5: {
      const Json& t = b["table"];
      if (!t.is_object() || !t.contains("columns") || !t["columns"].is_array() || !t.contains("rows") ||
          !t["rows"].is_array() || t["columns"].empty() || t["columns"].size() > kDocMaxColumns ||
          t["rows"].size() > kDocMaxRows) {
        return std::nullopt;
      }
      out.kind = DocBlock::Kind::table;
      std::vector<std::string>& ids = out.column_ids;
      for (const Json& c : t["columns"]) {
        if (!c.is_object() || !c.contains("id") || !c["id"].is_string() || !c.contains("label") ||
            !c["label"].is_string()) {
          return std::nullopt;
        }
        ids.push_back(c["id"].get<std::string>());
        out.column_labels.push_back(c["label"].get<std::string>());
      }
      for (const Json& r : t["rows"]) {
        if (!r.is_object()) {
          continue;
        }
        std::vector<std::string> cells;
        for (const std::string& id : ids) {
          cells.push_back(cell_words(r, id));
        }
        out.rows.push_back(std::move(cells));
      }
      return out;
    }
    default:
      return std::nullopt;
  }
}

Json spans_json(const std::vector<DocSpan>& spans) {
  static constexpr std::array<const char*, 4> kStyle = {"plain", "strong", "code", "link"};
  Json out = Json::array();
  for (const DocSpan& s : spans) {
    Json j = Json::object({{"text", s.text}, {"style", kStyle[static_cast<std::size_t>(s.style)]}});
    if (s.style == DocSpan::Style::link) {
      j["doc"] = s.doc;
      if (!s.heading.empty()) {
        j["heading"] = s.heading;
      }
    }
    out.push_back(std::move(j));
  }
  return out;
}

}  // namespace

DocumentView parse_document(const Json& body) {
  DocumentView doc;
  if (!body.is_object() || !body.contains("blocks") || !body["blocks"].is_array()) {
    doc.error = DocumentView::Error::not_a_document;
    return doc;
  }
  if (body.dump().size() > kDocMaxBytes) {
    doc.error = DocumentView::Error::too_large;
    return doc;
  }
  if (body.contains("title") && body["title"].is_string() && chars(body["title"].get<std::string>()) <= kDocMaxHeading) {
    doc.title = body["title"].get<std::string>();
  }
  std::size_t seen = 0;
  for (const Json& b : body["blocks"]) {
    if (seen++ >= kDocMaxBlocks) {
      break;
    }
    if (auto block = block_of(b)) {
      doc.blocks.push_back(std::move(*block));
    }
  }
  return doc;
}

Json document_json(const DocumentView& doc) {
  Json out = Json::object({{"title", doc.title}, {"blocks", Json::array()}});
  for (const DocBlock& b : doc.blocks) {
    Json j;
    switch (b.kind) {
      case DocBlock::Kind::heading:
        j = Json::object({{"kind", "heading"}, {"level", b.level}, {"text", b.text}});
        break;
      case DocBlock::Kind::paragraph:
        j = Json::object({{"kind", "paragraph"}, {"spans", spans_json(b.spans)}});
        break;
      case DocBlock::Kind::note:
        j = Json::object({{"kind", "note"}, {"spans", spans_json(b.spans)}});
        break;
      case DocBlock::Kind::list: {
        Json items = Json::array();
        for (const auto& item : b.items) {
          items.push_back(spans_json(item));
        }
        j = Json::object({{"kind", "list"}, {"ordered", b.ordered}, {"items", items}});
        break;
      }
      case DocBlock::Kind::code:
        j = Json::object({{"kind", "code"}, {"text", b.text}});
        break;
      case DocBlock::Kind::table: {
        Json columns = Json::array();
        for (std::size_t k = 0; k < b.column_ids.size(); ++k) {
          columns.push_back(Json::object({{"id", b.column_ids[k]}, {"label", b.column_labels[k]}}));
        }
        j = Json::object({{"kind", "table"}, {"columns", columns}, {"rows", b.rows}});
        break;
      }
    }
    out["blocks"].push_back(std::move(j));
  }
  if (doc.error == DocumentView::Error::too_large) {
    out["error"] = "too_large";
  } else if (doc.error == DocumentView::Error::not_a_document) {
    out["error"] = "not_a_document";
  }
  return out;
}

}  // namespace grace::view
