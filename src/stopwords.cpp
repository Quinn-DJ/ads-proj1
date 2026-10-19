// stopwords.cpp -- implementation of the stop word list loader.

#include "stopwords.hpp"

#include <fstream>
#include <stdexcept>

namespace mse {

// Read a stop word list from disk.
//
// Format: one lowercase word per line; blank lines and lines starting with
// '#' (the method/source header) are ignored. A trailing '\r' is tolerated so
// a CRLF checkout behaves the same as the committed LF files. Words are stored
// exactly as written -- no stemming or case folding happens here, because the
// tokenizer already lowercases its output and the list is authored to match.
// Throws std::runtime_error naming the path when the file cannot be opened.
// Complexity: O(file size) time, O(#words) memory.
StopWords StopWords::load(const std::string& path) {
    std::ifstream in(path);
    if (!in) {
        throw std::runtime_error("cannot open stop word list: " + path);
    }

    StopWords result;
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();  // tolerate CRLF endings
        }
        if (line.empty() || line.front() == '#') {
            continue;
        }
        result.words_.insert(line);
    }
    return result;
}

// True when `term` is in the list. Average O(1) via the hash set; the caller
// is responsible for passing a *normalized* token (lowercase, as produced by
// the tokenizer), because the comparison is exact.
bool StopWords::contains(const std::string& term) const {
    return words_.find(term) != words_.end();
}

}  // namespace mse
