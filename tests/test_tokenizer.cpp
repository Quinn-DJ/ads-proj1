// test_tokenizer.cpp -- unit tests for the tokenizer (run via `ctest --test-dir
// build`).
//
// The table covers every edge case listed in plan.md 3.4: contraction
// apostrophes (internal / leading / trailing / doubled), hyphens, digits,
// punctuation-only input, case folding, plus one document-level test that
// checks the keepHeadings / keepSpeakers / keepStage options.

#include <string>
#include <vector>

#include "catch_amalgamated.hpp"  // vendored Catch2 v3, single include
#include "tokenizer.hpp"

namespace {

std::vector<std::string> textsOf(const std::vector<mse::Token>& tokens) {
  std::vector<std::string> texts;
  texts.reserve(tokens.size());
  for (const mse::Token& token : tokens) {
    texts.push_back(token.text);
  }
  return texts;
}

}  // namespace

// Tokenizer spec from plan.md 3.4. Each checkText() call asserts the exact
// token sequence -- not merely that tokenization does not crash -- so a change
// in separator or apostrophe handling shows up immediately. The sections cover
// the cases that actually occur in the Shakespeare corpus plus the documented
// edge behaviors (digits, leading/trailing apostrophes, hyphens).
TEST_CASE("text tokenization covers the plan.md 3.4 edge cases",
          "[tokenizer]") {
  const mse::Tokenizer tokenizer;

  const auto checkText = [&](const std::string& input,
                             const std::vector<std::string>& expected) {
    INFO("input: \"" << input << "\"");
    std::vector<mse::Token> tokens;
    tokenizer.tokenizeText(input, 0, tokens);
    CHECK(textsOf(tokens) == expected);
  };

  // Apostrophes between letters belong to the token; this is what makes
  // "don't" and "o'er" survive as single units for the stemmer.
  SECTION("contraction apostrophes stay inside words") {
    checkText("Who's there?", {"who's", "there"});
    checkText("don't", {"don't"});
    checkText("marry'd", {"marry'd"});
    checkText("o'er", {"o'er"});
    checkText("ne'er", {"ne'er"});
    checkText("a'b'c", {"a'b'c"});
  }

  SECTION("leading / trailing / doubled apostrophes are separators") {
    checkText("'Tis now struck twelve;", {"tis", "now", "struck", "twelve"});
    checkText("friends'", {"friends"});
    checkText("'sblood,", {"sblood"});
    // Two adjacent apostrophes: the first ends the token "don" (trailing),
    // the second is a leading apostrophe and is skipped, leaving "t".
    checkText("don''t", {"don", "t"});
  }

  SECTION("hyphens and dashes split words") {
    checkText("to-day", {"to", "day"});
    checkText("well--well", {"well", "well"});
  }

  // Digits are separators, which is why "1660s" yields only "s" -- a known
  // edge case documented in notes.md rather than a desired word.
  SECTION("digits are not tokens") {
    checkText("5,000", {});
    // The digits 1660 are separators; only the trailing letter survives.
    checkText("1660s", {"s"});
  }

  SECTION("punctuation-only and empty input yield nothing") {
    checkText("-- ,.!? --", {});
    checkText("", {});
  }

  // Case folding must be ASCII-only and deterministic across platforms.
  SECTION("case folding") {
    checkText(
        "To be, or not to be: that is the question.",
        {"to", "be", "or", "not", "to", "be", "that", "is", "the", "question"});
    // Uppercase input and trailing punctuation are both normalized away.
    checkText("KING.", {"king"});
  }
}

// Document-level filtering. The default must include only Body lines; the
// three keep-* flags must include their line kind. Token::line is also checked
// because --pretty relies on it to map a position back to a source line.
TEST_CASE("document tokenization honours the line-kind options",
          "[tokenizer]") {
  mse::Document doc;
  doc.lines = {
      {mse::LineKind::Heading, "ACT I"},
      {mse::LineKind::Stage, "Exit Ghost"},
      {mse::LineKind::Speaker, "BERNARDO"},
      {mse::LineKind::Body, "Who's there?"},
  };

  SECTION("default options tokenize only body lines") {
    std::vector<mse::Token> tokens;
    const mse::Tokenizer defaultTokenizer;
    defaultTokenizer.tokenize(doc, tokens);

    INFO("texts: [" << [&] {
      std::string joined;
      for (const mse::Token& token : tokens) {
        joined += token.text + " ";
      }
      return joined;
    }() << "]");
    REQUIRE(tokens.size() == 2);
    CHECK(tokens[0].text == "who's");
    CHECK(tokens[0].line == 3);
    CHECK(tokens[1].text == "there");
    CHECK(tokens[1].line == 3);
  }

  SECTION("keepHeadings/keepSpeakers/keepStage include everything") {
    mse::TokenizerOptions options;
    options.keepHeadings = true;
    options.keepSpeakers = true;
    options.keepStage = true;
    const mse::Tokenizer allTokenizer(options);
    std::vector<mse::Token> tokens;
    allTokenizer.tokenize(doc, tokens);

    CHECK(textsOf(tokens) == std::vector<std::string>{"act", "i", "exit",
                                                      "ghost", "bernardo",
                                                      "who's", "there"});
  }
}
