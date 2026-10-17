// test_tokenizer.cpp -- unit tests for the tokenizer (run via `ctest --test-dir build`).
//
// The table covers every edge case listed in plan.md 3.4: contraction
// apostrophes (internal / leading / trailing / doubled), hyphens, digits,
// punctuation-only input, case folding, plus one document-level test that
// checks the keepHeadings / keepSpeakers / keepStage options.

#include "catch_amalgamated.hpp"  // vendored Catch2 v3, single include

#include "tokenizer.hpp"

#include <string>
#include <vector>

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

TEST_CASE("text tokenization covers the plan.md 3.4 edge cases", "[tokenizer]") {
    const mse::Tokenizer tokenizer;

    const auto checkText = [&](const std::string& input,
                               const std::vector<std::string>& expected) {
        INFO("input: \"" << input << "\"");
        std::vector<mse::Token> tokens;
        tokenizer.tokenizeText(input, 0, tokens);
        CHECK(textsOf(tokens) == expected);
    };

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
        checkText("don''t", {"don", "t"});
    }

    SECTION("hyphens and dashes split words") {
        checkText("to-day", {"to", "day"});
        checkText("well--well", {"well", "well"});
    }

    SECTION("digits are not tokens") {
        checkText("5,000", {});
        checkText("1660s", {"s"});
    }

    SECTION("punctuation-only and empty input yield nothing") {
        checkText("-- ,.!? --", {});
        checkText("", {});
    }

    SECTION("case folding") {
        checkText("To be, or not to be: that is the question.",
                  {"to", "be", "or", "not", "to", "be", "that", "is", "the", "question"});
        checkText("KING.", {"king"});
    }
}

TEST_CASE("document tokenization honours the line-kind options", "[tokenizer]") {
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

        CHECK(textsOf(tokens) ==
              std::vector<std::string>{
                  "act", "i", "exit", "ghost", "bernardo", "who's", "there"});
    }
}
