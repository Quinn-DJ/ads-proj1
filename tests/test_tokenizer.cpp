// test_tokenizer.cpp -- unit tests for the tokenizer (run via `ctest --test-dir build`).
//
// The table covers every edge case listed in plan.md 3.4: contraction
// apostrophes (internal / leading / trailing / doubled), hyphens, digits,
// punctuation-only input, case folding, plus one document-level test that
// checks the keepHeadings / keepSpeakers / keepStage options.

#include "tokenizer.hpp"

#include <iostream>
#include <string>
#include <vector>

namespace {

int failures = 0;

void report(const std::string& name, bool ok) {
    if (ok) {
        std::cout << "ok    " << name << "\n";
    } else {
        std::cout << "FAIL  " << name << "\n";
        ++failures;
    }
}

std::string join(const std::vector<std::string>& words) {
    std::string result;
    for (std::size_t i = 0; i < words.size(); ++i) {
        if (i > 0) result += ' ';
        result += words[i];
    }
    return result;
}

// Tokenize one text and compare against the expected token list.
void checkText(const std::string& input, const std::vector<std::string>& expected) {
    const mse::Tokenizer tokenizer;
    std::vector<mse::Token> tokens;
    tokenizer.tokenizeText(input, 0, tokens);

    std::vector<std::string> got;
    for (const mse::Token& token : tokens) {
        got.push_back(token.text);
    }
    const bool ok = got == expected;
    if (!ok) {
        std::cout << "      got      [" << join(got) << "]\n"
                  << "      expected [" << join(expected) << "]\n";
    }
    report("\"" + input + "\"", ok);
}

void checkDocumentOptions() {
    mse::Document doc;
    doc.lines = {
        {mse::LineKind::Heading, "ACT I"},
        {mse::LineKind::Stage, "Exit Ghost"},
        {mse::LineKind::Speaker, "BERNARDO"},
        {mse::LineKind::Body, "Who's there?"},
    };

    // default options: only the body line is tokenized
    std::vector<mse::Token> tokens;
    const mse::Tokenizer defaultTokenizer;
    defaultTokenizer.tokenize(doc, tokens);
    bool ok = tokens.size() == 2 &&
              tokens[0].text == "who's" && tokens[0].line == 3 &&
              tokens[1].text == "there" && tokens[1].line == 3;
    report("document: default options use only body lines", ok);

    // all line kinds enabled
    mse::TokenizerOptions options;
    options.keepHeadings = true;
    options.keepSpeakers = true;
    options.keepStage = true;
    const mse::Tokenizer allTokenizer(options);
    tokens.clear();
    allTokenizer.tokenize(doc, tokens);

    const std::vector<std::string> expected = {
        "act", "i", "exit", "ghost", "bernardo", "who's", "there"};
    ok = tokens.size() == expected.size();
    for (std::size_t i = 0; ok && i < tokens.size(); ++i) {
        ok = tokens[i].text == expected[i];
    }
    report("document: keepHeadings/keepSpeakers/keepStage include everything", ok);
}

}  // namespace

int main() {
    // plan.md 3.4 edge cases
    checkText("Who's there?", {"who's", "there"});
    checkText("'Tis now struck twelve;", {"tis", "now", "struck", "twelve"});
    checkText("To be, or not to be: that is the question.",
              {"to", "be", "or", "not", "to", "be", "that", "is", "the", "question"});
    checkText("to-day", {"to", "day"});
    checkText("well--well", {"well", "well"});
    checkText("friends'", {"friends"});
    checkText("don't", {"don't"});
    checkText("marry'd", {"marry'd"});
    checkText("o'er", {"o'er"});
    checkText("ne'er", {"ne'er"});
    checkText("'sblood,", {"sblood"});
    checkText("KING.", {"king"});
    checkText("5,000", {});
    checkText("1660s", {"s"});
    checkText("-- ,.!? --", {});
    checkText("", {});
    checkText("a'b'c", {"a'b'c"});
    checkText("don''t", {"don", "t"});

    checkDocumentOptions();

    if (failures == 0) {
        std::cout << "\nall tokenizer tests passed\n";
        return 0;
    }
    std::cout << "\n" << failures << " test(s) failed\n";
    return 1;
}
