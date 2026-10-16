// test_stopwords.cpp -- unit tests for the stop word list loader.
//
// Run via `ctest --test-dir build`; CTest sets the working directory to the
// project root, so the list is read from data/stopwords.txt.

#include "stopwords.hpp"

#include <exception>
#include <iostream>
#include <string>

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

}  // namespace

int main() {
    const mse::StopWords words = mse::StopWords::load("data/stopwords.txt");

    report("list contains 39 structural words", words.size() == 39);
    report("'the' is a stop word", words.contains("the"));
    report("'not' is a stop word", words.contains("not"));
    report("'what' is a stop word", words.contains("what"));
    report("'yet' is a stop word", words.contains("yet"));
    report("'thou' is kept searchable (archaic, deferred)",
           !words.contains("thou"));
    report("'hath' is kept searchable (archaic, deferred)",
           !words.contains("hath"));
    report("'love' is kept searchable (content word)", !words.contains("love"));
    report("'time' is kept searchable (content word)", !words.contains("time"));
    report("'king' is kept searchable", !words.contains("king"));

    // missing files must fail loudly, naming the path
    bool threw = false;
    try {
        mse::StopWords::load("data/no_such_stopword_file.txt");
    } catch (const std::exception& error) {
        threw = std::string(error.what()).find("no_such_stopword_file") !=
                std::string::npos;
    }
    report("missing file throws with the path in the message", threw);

    if (failures == 0) {
        std::cout << "\nall stopword tests passed\n";
        return 0;
    }
    std::cout << "\n" << failures << " test(s) failed\n";
    return 1;
}
