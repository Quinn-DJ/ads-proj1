// tokenizer.cpp -- implementation of the tokenizer (see tokenizer.hpp).

#include "tokenizer.hpp"

namespace mse {

namespace {

inline bool isLetter(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

inline char toLower(char c) {
    return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
}

}  // namespace

Tokenizer::Tokenizer(const TokenizerOptions& options) : options_(options) {}

bool Tokenizer::wants(LineKind kind) const {
    switch (kind) {
        case LineKind::Body:    return true;
        case LineKind::Heading: return options_.keepHeadings;
        case LineKind::Speaker: return options_.keepSpeakers;
        case LineKind::Stage:   return options_.keepStage;
    }
    return false;
}

void Tokenizer::tokenizeText(const std::string& text, std::uint32_t line,
                             std::vector<Token>& out) const {
    const std::size_t n = text.size();
    std::size_t i = 0;
    while (i < n) {
        // skip anything that cannot start a token: separators, digits,
        // leading apostrophes, ...
        if (!isLetter(text[i])) {
            ++i;
            continue;
        }

        // scan one token: letters plus apostrophes that sit between letters
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
