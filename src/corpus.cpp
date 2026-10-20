// corpus.cpp -- implementation of the document storage layer (see corpus.hpp).

#include "corpus.hpp"

#include <fstream>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace mse {

namespace {

// ---------------------------------------------------------------------------
// file / string helpers
// ---------------------------------------------------------------------------

// Read an entire file as bytes.
//
// The file is opened in binary mode so the byte sequence is preserved exactly;
// line ending handling is left to splitLines(). Throws std::runtime_error
// naming the path on any I/O failure, so callers can always tell *which*
// input is broken. Complexity: O(file size) time and memory.
std::string readFile(const std::string& path) {
  std::ifstream in(path, std::ios::binary);
  if (!in) {
    throw std::runtime_error("cannot open file: " + path);
  }
  std::ostringstream buffer;
  buffer << in.rdbuf();
  if (in.bad()) {
    throw std::runtime_error("error while reading file: " + path);
  }
  return buffer.str();
}

// Split `data` on '\n' and drop an optional trailing '\r' from every line.
//
// The generated text layer uses '\n' only, but tolerating CRLF costs one
// comparison per line and lets a checkout with Windows line endings still
// load. A final line without a trailing newline is emitted as well; empty
// lines are preserved because they carry position (line numbers) for --pretty.
// Complexity: O(bytes).
std::vector<std::string> splitLines(const std::string& data) {
  std::vector<std::string> lines;
  std::string current;
  for (const char ch : data) {
    if (ch == '\n') {
      if (!current.empty() && current.back() == '\r') {
        current.pop_back();
      }
      lines.push_back(current);
      current.clear();
    } else {
      current.push_back(ch);
    }
  }
  if (!current.empty()) {
    lines.push_back(current);
  }
  return lines;
}

// Split one manifest row on tabs.
//
// Titles may contain spaces but never a tab (the manifest producers
// -- tools/extract.py and tools/import_corpus.py -- guarantee this), so a
// plain split is sufficient; unlike a CSV parser we deliberately do not
// support quoting or escaped tabs. The trailing empty field is kept, because
// the caller checks the exact field count (the optional 8th column is the
// text format).
std::vector<std::string> splitTab(const std::string& line) {
  std::vector<std::string> fields;
  std::string current;
  for (const char ch : line) {
    if (ch == '\t') {
      fields.push_back(current);
      current.clear();
    } else {
      current.push_back(ch);
    }
  }
  fields.push_back(current);
  return fields;
}

// Join a directory prefix and a relative manifest path.
//
// Handles the three forms that occur in practice: an empty/"." prefix (the
// path is already relative), a prefix that already ends in '/', and the normal
// case. Only forward slashes are considered because manifest paths are
// generated with them on every platform.
std::string joinPaths(const std::string& dir, const std::string& rel) {
  if (dir.empty() || dir == ".") {
    return rel;
  }
  if (dir.back() == '/') {
    return dir + rel;
  }
  return dir + "/" + rel;
}

// Directory part of a path: "a/b/c" -> "a/b", "c" -> ".", "/c" -> "/".
// Both '/' and '\' are accepted so Windows-style paths passed on the command
// line also resolve. Used twice in Corpus::load() to recover the project root
// from <root>/data/manifest.tsv; no normalization or symlink resolution is
// needed because the manifest stores plain relative paths.
std::string dirName(const std::string& path) {
  const std::size_t pos = path.find_last_of("/\\");
  if (pos == std::string::npos) {
    return ".";
  }
  if (pos == 0) {
    return "/";
  }
  return path.substr(0, pos);
}

// Classify one line of a data/text/*.txt file and strip its marker.
//
// A line is a heading only when it starts with '[' AND ends with ']', and a
// stage line only when it starts with '(' AND ends with ')'. This "both ends"
// rule is essential: the corpus contains hundreds of verse lines that merely
// start with '[' (for example "[To HELENA] The best wishes that can be forged
// in"), and treating those as marker lines would silently drop real text.
// Speaker labels start with '@' and have no closing marker. Everything else is
// body text. The marker itself is removed, so callers never see it.
LineKind classifyLine(const std::string& line, std::string& content) {
  if (line.size() >= 2 && line.front() == '[' && line.back() == ']') {
    content = line.substr(1, line.size() - 2);
    return LineKind::Heading;
  }
  if (line.size() >= 2 && line.front() == '(' && line.back() == ')') {
    content = line.substr(1, line.size() - 2);
    return LineKind::Stage;
  }
  if (!line.empty() && line.front() == '@') {
    content = line.substr(1);
    return LineKind::Speaker;
  }
  content = line;
  return LineKind::Body;
}

}  // namespace

// Human-readable name of a line kind (used by stats/debug output and tests).
const char* toString(LineKind kind) {
  switch (kind) {
    case LineKind::Body:
      return "body";
    case LineKind::Heading:
      return "heading";
    case LineKind::Stage:
      return "stage";
    case LineKind::Speaker:
      return "speaker";
  }
  return "unknown";
}

const char* toString(TextFormat format) {
  switch (format) {
    case TextFormat::Marked:
      return "marked";
    case TextFormat::Plain:
      return "plain";
  }
  return "unknown";
}

// Parse the optional 8th manifest column. Absent means "marked", so every
// existing manifest (including the golden4 fixture) keeps working unchanged.
TextFormat parseTextFormat(const std::string& value, const std::string& where) {
  if (value == "marked") {
    return TextFormat::Marked;
  }
  if (value == "plain") {
    return TextFormat::Plain;
  }
  throw std::runtime_error(where + ": unknown text format '" + value +
                           "' (expected marked|plain)");
}

// Load the manifest and every text file it references.
//
// The loader is intentionally strict: the manifest header is part of the
// contract with the manifest producers, docIDs must be dense (1..N) because
// byID() indexes directly, and every referenced text file must exist. Any
// violation throws std::runtime_error with the offending file/row, which is
// far more useful than silently producing a partially loaded corpus.
//
// The optional 8th manifest column selects the text format: "marked" (the
// Shakespeare layer, with [heading]/(stage)/@speaker markers) or "plain"
// (one Body line per line, used by the generic corpus importer and the RFC
// demo). Omitting the column preserves the original Shakespeare behavior.
//
// Complexity: O(total bytes) time and memory. The whole corpus stays resident
// (about 5 MB of text for the Shakespeare set); the scaling discussion lives
// in the Bonus analysis.
Corpus Corpus::load(const std::string& manifestPath) {
  const std::string manifest = readFile(manifestPath);

  // The manifest lives at <root>/data/manifest.tsv; paths inside it are
  // relative to <root>, so two dirName() steps recover the project root.
  const std::string root = dirName(dirName(manifestPath));

  const std::vector<std::string> rows = splitLines(manifest);
  // Refuse a mismatched header early: the parser reads columns by position,
  // so a header mismatch would otherwise turn into confusing field errors.
  const std::string expectedHeader =
      "docid\tkind\tcategory\ttitle\ttext\tsource\turl";
  if (rows.empty() ||
      rows.front().compare(0, expectedHeader.size(), expectedHeader) != 0) {
    throw std::runtime_error("unexpected manifest header in " + manifestPath);
  }
  // Optional 8th column "format" (marked|plain); anything else is rejected
  // so a typo cannot silently fall back to the default.
  const bool hasFormatColumn = rows.front() == expectedHeader + "\tformat";
  if (!hasFormatColumn && rows.front() != expectedHeader) {
    throw std::runtime_error("unexpected manifest header in " + manifestPath +
                             " (optional 8th column must be 'format')");
  }

  Corpus corpus;
  corpus.docs_.reserve(rows.size() - 1);

  for (std::size_t i = 1; i < rows.size(); ++i) {
    if (rows[i].empty()) {
      continue;
    }
    const std::string where = manifestPath + " row " + std::to_string(i + 1);

    const std::vector<std::string> fields = splitTab(rows[i]);
    const std::size_t expectedFields = hasFormatColumn ? 8 : 7;
    if (fields.size() != expectedFields) {
      throw std::runtime_error(
          where + ": expected " + std::to_string(expectedFields) +
          " tab-separated fields, got " + std::to_string(fields.size()));
    }

    // Parse one row into a Document. std::stoi is wrapped so a malformed
    // ID produces a message that names the row instead of an uncaught
    // std::invalid_argument.
    Document doc;
    try {
      doc.id = std::stoi(fields[0]);
    } catch (const std::exception&) {
      throw std::runtime_error(where + ": docID is not a number: '" +
                               fields[0] + "'");
    }
    doc.kind = fields[1];
    doc.category = fields[2];
    doc.title = fields[3];
    doc.textPath = fields[4];
    doc.sourcePath = fields[5];
    doc.url = fields[6];
    if (hasFormatColumn) {
      doc.format = parseTextFormat(fields[7], where);
    }

    // docIDs must be dense and ordered 1..N: byID() computes docs_[id-1],
    // and the index format assumes docs[i].docID == i+1. Rejecting gaps
    // here keeps that invariant true for every later stage.
    if (doc.id != static_cast<int>(corpus.docs_.size()) + 1) {
      throw std::runtime_error(where + ": docID " + std::to_string(doc.id) +
                               " is not in sequence (expected " +
                               std::to_string(corpus.docs_.size() + 1) + ")");
    }

    // Load the line-based text. '#' lines are producer header comments
    // (docID/title/source) and empty lines carry no text; both are skipped
    // so that Document::lines contains only searchable/annotated units.
    // Marked documents get marker classification; plain documents skip it
    // entirely, so "(a remark)" or "[note]" in ordinary prose stays
    // searchable instead of being mistaken for a stage/heading marker.
    // The line index inside Document::lines is what --pretty later maps a
    // filtered-stream position back to.
    const std::string textFile = joinPaths(root, doc.textPath);
    for (const std::string& line : splitLines(readFile(textFile))) {
      if (line.empty() || line.front() == '#') {
        continue;
      }
      if (doc.format == TextFormat::Plain) {
        doc.lines.push_back(TextLine{LineKind::Body, line});
        continue;
      }
      std::string content;
      const LineKind kind = classifyLine(line, content);
      doc.lines.push_back(TextLine{kind, std::move(content)});
    }

    corpus.docs_.push_back(std::move(doc));
  }
  return corpus;
}

// 1-based document lookup. The dense-ID check in load() makes this a direct
// index (O(1)); an out-of-range ID is a caller error, so throw rather than
// return a sentinel or clamp.
const Document& Corpus::byID(int docID) const {
  if (docID < 1 || static_cast<std::size_t>(docID) > docs_.size()) {
    throw std::out_of_range("docID out of range: " + std::to_string(docID));
  }
  return docs_[static_cast<std::size_t>(docID) - 1];
}

// Walk every line once and accumulate storage statistics.
//
// Words are counted as runs of non-whitespace, the same rule tools/extract.py
// uses, so the C++ numbers can be compared directly with the Python report
// (930,911 words for the committed corpus). Note that this is *not* the token
// count used by the index: the tokenizer ignores digits/punctuation and
// filters marker lines, which is why 866,626 tokens is a different number.
// Complexity: O(total characters), O(1) extra space.
Corpus::Stats Corpus::stats() const {
  Stats stats;
  stats.documents = docs_.size();
  for (const Document& doc : docs_) {
    for (const TextLine& line : doc.lines) {
      ++stats.lines;
      ++stats.linesByKind[static_cast<std::size_t>(line.kind)];
      stats.bytes += line.text.size();
      // Count whitespace-separated fields, the same rule the Python
      // preprocessing report uses (tools/extract.py). `inWord` implements
      // a simple run-length scan: increment on a false->true transition.
      bool inWord = false;
      for (const char ch : line.text) {
        const bool isSpace = ch == ' ' || ch == '\t' || ch == '\r' ||
                             ch == '\n' || ch == '\f' || ch == '\v';
        if (!isSpace && !inWord) {
          ++stats.words;
          inWord = true;
        } else if (isSpace) {
          inWord = false;
        }
      }
    }
  }
  return stats;
}

}  // namespace mse
