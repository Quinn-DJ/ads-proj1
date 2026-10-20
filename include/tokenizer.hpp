// tokenizer.hpp -- text -> normalized token stream.
//
// Rules (plan.md 3.4; the corpus is 100% ASCII, verified 2026-10-10):
//   * a token is a run of ASCII letters; digits and all other punctuation
//     (including hyphens) act as separators:  to-day -> to, day
//   * an apostrophe is kept only *inside* a token (between letters):
//     don't -> don't,  'Tis -> tis,  friends' -> friends
//   * every token is lowercased:  KING. -> king
//   * the tokenizer never removes stopwords and never stems -- those are
//     separate stages (stopwords / stemmer modules), so a bug can always be
//     traced to exactly one stage
//
// Which lines of a document contribute tokens is decided by TokenizerOptions:
// by default only Body lines (the actual verse/prose) are used; headings,
// stage directions and speaker labels can be included to reproduce the
// word-count experiments of project task (1).

#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "corpus.hpp"

namespace mse {

struct TokenizerOptions {
  bool keepHeadings = false;  // [ACT I] / [SCENE ...] lines
  bool keepSpeakers = false;  // @BERNARDO lines
  bool keepStage = false;     // (Exit Ghost) lines
};

struct Token {
  std::string text;        // normalized token, e.g. "who's", "king"
  std::uint32_t line = 0;  // index into Document::lines it came from
};

class Tokenizer {
 public:
  explicit Tokenizer(const TokenizerOptions& options = TokenizerOptions{});

  // Append the tokens of a whole document, in reading order.
  void tokenize(const Document& doc, std::vector<Token>& out) const;

  // Tokenize one line of plain text (marker already stripped). Exposed for
  // unit tests, and later reused by the query side for input normalization.
  void tokenizeText(const std::string& text, std::uint32_t line,
                    std::vector<Token>& out) const;

  // Whether lines of this kind contribute tokens under the options.
  bool wants(LineKind kind) const;

 private:
  TokenizerOptions options_;
};

}  // namespace mse
