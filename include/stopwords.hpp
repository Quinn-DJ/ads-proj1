// stopwords.hpp -- stop word list for the mini search engine.
//
// Loads data/stopwords.txt (one lowercase word per line; lines starting with
// '#' are comments, blank lines are ignored) into a hash set, so the word
// counter and -- later -- the index builder can ask "is this token a stop
// word?" in O(1).
//
// The list itself is *derived from our own corpus statistics* (see the
// header of data/stopwords.txt and notes.md 3.6). This module only consumes
// it; regenerating the list is an offline analysis step, not a runtime one.

#pragma once

#include <cstddef>
#include <string>
#include <unordered_set>

namespace mse {

class StopWords {
public:
    // Read a stop word list. Comment ('#') and blank lines are skipped;
    // words are stored exactly as written (the list is lowercase, matching
    // the tokenizer's output). Throws std::runtime_error naming the file
    // when it cannot be read.
    static StopWords load(const std::string& path);

    // True when the *normalized* token (as produced by the tokenizer) is a
    // stop word. Average O(1).
    bool contains(const std::string& term) const;

    std::size_t size() const { return words_.size(); }

private:
    std::unordered_set<std::string> words_;
};

}  // namespace mse
