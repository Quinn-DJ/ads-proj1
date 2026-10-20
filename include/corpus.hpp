// corpus.h -- document storage for the mini search engine.
//
// The engine is corpus-agnostic: it reads a manifest plus the line-based
// text files the manifest points to, and treats kind/category as opaque
// strings (any vocabulary works -- `mse stats` just groups by them). What
// the Shakespeare producer (tools/extract.py) freezes into data/ is one
// *instance* of the format, not engine knowledge.
//
//   data/manifest.tsv   one row per document (docID, kind, category, title,
//   ...) data/text/*.txt     the line-based text of each document
// This module loads both into memory. The whole corpus is ~5.5 MB of text, so
// a resident copy is the simplest and fastest design (plan.md section 6.1);
// the scaling question (500k files / 400M terms) is discussed separately in
// the bonus analysis.
//
// Per-document text format (optional 8th manifest column, default "marked"):
//   marked -- the extract.py layer: [ACT I] headings, (stage) directions and
//             @SPEAKER labels are tagged and filtered by default
//   plain  -- no line markers: every non-comment line is searchable body
//             text (prose, letters, logs, transcripts, ...). A plain corpus
//             never loses a parenthetical sentence to the stage rule.
//             '#' comment lines are skipped in both formats.

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace mse {

// Kind of a line inside a data/text/*.txt file (see data/text/README.md).
enum class LineKind : std::uint8_t {
  Body,     // verse line or prose -- the searchable text
  Heading,  // [ACT I], [SCENE I. ...]
  Stage,    // (Exit Ghost)
  Speaker,  // @BERNARDO
};

const char* toString(LineKind kind);

// Line-marker convention of one document's text file (manifest column 8).
enum class TextFormat : std::uint8_t {
  Marked,  // extract.py's marker layer (the Shakespeare producer)
  Plain,   // no markers: everything non-'#' is body text
};

const char* toString(TextFormat format);

// One line of a document, with its marker already stripped:
//   [ACT I]       -> Heading "ACT I"
//   (Exit Ghost)  -> Stage   "Exit Ghost"
//   @BERNARDO     -> Speaker "BERNARDO"
//   Who's there?  -> Body    (unchanged)
// Plain-format documents only ever produce Body lines.
struct TextLine {
  LineKind kind = LineKind::Body;
  std::string text;
};

// One document = one work-sized unit (play / sonnet / chapter / article --
// whatever unit the manifest's producer decided, plan.md 3.2).
struct Document {
  int id = 0;              // 1-based docID from the manifest
  std::string kind;        // opaque producer vocabulary (play|sonnet|...)
  std::string category;    // opaque producer vocabulary (Comedy|...)
  std::string title;       // display name, e.g. "Hamlet", "RFC 7231"
  std::string textPath;    // data/text/....txt (relative to project root)
  std::string sourcePath;  // original file the text was extracted from
  std::string url;         // canonical URL of the source (or "-")
  TextFormat format = TextFormat::Marked;  // line-marker convention
  std::vector<TextLine> lines;
};

// In-memory copy of the whole corpus.
class Corpus {
 public:
  // Loads the manifest and every text file it references.
  // Paths inside the manifest are resolved against the project root, which
  // is derived from the manifest location: <root>/data/manifest.tsv.
  // Throws std::runtime_error naming the offending file/row on any problem.
  static Corpus load(const std::string& manifestPath);

  std::size_t size() const { return docs_.size(); }
  const Document& operator[](std::size_t index) const {
    return docs_.at(index);
  }

  // 1-based lookup by docID. docIDs are validated to be dense (1..N) when
  // loading, so this is a direct index. Throws std::out_of_range otherwise.
  const Document& byID(int docID) const;

  // Storage statistics; used by `mse stats` and by tests to cross-check the
  // Python preprocessing numbers (152 632 lines / 930 911 words).
  struct Stats {
    std::size_t documents = 0;
    std::size_t lines = 0;
    std::size_t words =
        0;  // whitespace-separated fields, same rule as tools/extract.py
    std::size_t bytes = 0;  // total characters of all line texts
    std::array<std::size_t, 4> linesByKind{};  // indexed by LineKind
  };
  Stats stats() const;

 private:
  std::vector<Document> docs_;  // index i holds docID i+1
};

}  // namespace mse
