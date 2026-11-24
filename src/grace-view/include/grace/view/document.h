// SPDX-License-Identifier: MIT
// document.h: a document block's body (G44, D251), checked and put in one shape: its title and its blocks (headings,
// paragraphs, lists, notes, code, plain tables), each paragraph a list of spans (plain, strong, code, or a link to
// another document of the family, perhaps to one of its headings; never a URL). Pure. The browser view's
// web/src/model/document.ts does the same, and both run fixtures/document-cases.json. A block that breaks a rule is left
// out; a document over 64 KiB is not drawn at all.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "grace/protocol/codec.h"

namespace grace::view {

struct DocSpan {
  enum class Style : std::uint8_t { plain, strong, code, link };
  std::string text;
  Style style = Style::plain;
  std::string doc;      // a link's document
  std::string heading;  // and one of its headings, or none
};

struct DocBlock {
  enum class Kind : std::uint8_t { heading, paragraph, list, note, code, table };
  Kind kind = Kind::paragraph;
  int level = 2;                              // a heading's
  std::string text;                           // a heading's or a code block's
  std::vector<DocSpan> spans;                 // a paragraph's or a note's
  bool ordered = false;                       // a list's
  std::vector<std::vector<DocSpan>> items;    // a list's
  std::vector<std::string> column_ids;        // a table's
  std::vector<std::string> column_labels;
  std::vector<std::vector<std::string>> rows;  // a table's cells, as words
};

struct DocumentView {
  enum class Error : std::uint8_t { none, too_large, not_a_document };
  std::string title;
  std::vector<DocBlock> blocks;
  Error error = Error::none;
};

inline constexpr std::size_t kDocMaxBytes = 64 * 1024;
inline constexpr std::size_t kDocMaxBlocks = 400;
inline constexpr std::size_t kDocMaxText = 2000;      // a paragraph's, a list item's or a note's characters
inline constexpr std::size_t kDocMaxHeading = 120;
inline constexpr std::size_t kDocMaxCode = 16 * 1024;  // bytes
inline constexpr std::size_t kDocMaxItems = 100;
inline constexpr std::size_t kDocMaxColumns = 8;
inline constexpr std::size_t kDocMaxRows = 200;

DocumentView parse_document(const protocol::Json& body);

// The document as the shared cases write it, for the tests that compare the two views.
protocol::Json document_json(const DocumentView& doc);

}  // namespace grace::view
