// stopwords.cpp -- implementation of the stop word list loader.

#include "stopwords.hpp"

#include <fstream>
#include <stdexcept>

namespace mse {

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

bool StopWords::contains(const std::string& term) const {
    return words_.find(term) != words_.end();
}

}  // namespace mse
