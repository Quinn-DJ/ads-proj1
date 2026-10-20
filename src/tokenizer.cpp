// tokenizer.cpp -- implementation of the tokenizer (see tokenizer.hpp).

#include "tokenizer.hpp"

namespace mse {

namespace {

// ASCII letter test. The corpus was verified to contain zero non-ASCII
// characters (notes.md 1.8), so byte-wise classification is exact for this
// data; a Unicode corpus would need a different tokenizer.
inline bool isLetter(char c) {
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

// ASCII-only lowercasing. Deliberately avoids std::tolower and locales: the
// mapping must be identical on every platform, and only A-Z are involved.
inline char toLower(char c) {
  return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
}

}  // namespace

Tokenizer::Tokenizer(const TokenizerOptions& options) : options_(options) {}

// Decide whether lines of this kind contribute tokens.
//
// Body lines are always included; headings, stage directions and speaker
// labels are opt-in so the default index contains only the actual verse/prose.
// The flags exist to reproduce the Task 1 "with/without speakers" experiments
// without changing the text layer.
bool Tokenizer::wants(LineKind kind) const {
  switch (kind) {
    case LineKind::Body:
      return true;
    case LineKind::Heading:
      return options_.keepHeadings;
    case LineKind::Speaker:
      return options_.keepSpeakers;
    case LineKind::Stage:
      return options_.keepStage;
  }
  return false;
}

// Tokenize one line of plain text (marker already stripped).
//
// The scanner is a hand-written state machine over bytes. A token starts at an
// ASCII letter and continues over letters and internal apostrophes (an
// apostrophe followed by a letter); anything else -- digits, hyphens, spaces,
// punctuation, a trailing apostrophe -- ends the token. `line` is copied into
// every Token so --pretty can map a match back to its source line without
// re-tokenizing the document.
//
// Complexity: O(text length + token bytes). The output vector is appended to,
// allowing a caller to reuse it across documents.
void Tokenizer::tokenizeText(const std::string& text, std::uint32_t line,
                             std::vector<Token>& out) const {
  const std::size_t n = text.size();
  std::size_t i = 0;
  while (i < n) {
    // Skip anything that cannot start a token: separators, digits, leading
    // apostrophes, etc. A leading apostrophe is discarded ('Tis -> tis).
    if (!isLetter(text[i])) {
      ++i;
      continue;
    }

    // Scan one token: letters plus apostrophes that sit between letters.
    // The lookahead on text[j + 1] keeps a trailing apostrophe (friends')
    // out of the token, while internal ones (don't, o'er) are preserved.
    std::string token;
    std::size_t j = i;
    while (j < n) {
      const char c = text[j];
      if (isLetter(c)) {
        token.push_back(toLower(c));
        ++j;
        continue;
      }
      if (c == '\'' && j + 1 < n && isLetter(text[j + 1])) {
        token.push_back('\'');
        ++j;
        continue;
      }
      break;  // trailing apostrophe, hyphen, punctuation, digit, ...
    }
    out.push_back(Token{std::move(token), line});
    i = j;
  }
}

// Tokenize a whole document in reading order.
//
// Lines are filtered by Tokenizer::wants(); the line index is passed to
// tokenizeText() and stored in each token, so a position in the filtered token
// stream can later be traced back to the line it came from. Complexity:
// O(total document bytes + token count).
void Tokenizer::tokenize(const Document& doc, std::vector<Token>& out) const {
  for (std::size_t i = 0; i < doc.lines.size(); ++i) {
    const TextLine& line = doc.lines[i];
    if (!wants(line.kind)) {
      continue;
    }
    tokenizeText(line.text, static_cast<std::uint32_t>(i), out);
  }
}

}  // namespace mse
