// test_stopwords.cpp -- unit tests for the stop word list loader.
//
// Run via `ctest --test-dir build`; CTest sets the working directory to the
// project root, so the list is read from data/stopwords.txt.

#include "catch_amalgamated.hpp"  // vendored Catch2 v3, single include

#include "stopwords.hpp"

#include <exception>
#include <string>

TEST_CASE("the list contains the 39 structural words", "[stopwords]") {
    const mse::StopWords words = mse::StopWords::load("data/stopwords.txt");

    CHECK(words.size() == 39);
    CHECK(words.contains("the"));
    CHECK(words.contains("not"));
    CHECK(words.contains("what"));
    CHECK(words.contains("yet"));
}

TEST_CASE("content and archaic words stay searchable", "[stopwords]") {
    const mse::StopWords words = mse::StopWords::load("data/stopwords.txt");

    CHECK_FALSE(words.contains("thou"));  // archaic, deferred
    CHECK_FALSE(words.contains("hath"));  // archaic, deferred
    CHECK_FALSE(words.contains("love"));  // content word
    CHECK_FALSE(words.contains("time"));  // content word
    CHECK_FALSE(words.contains("king"));
}

TEST_CASE("missing files fail loudly, naming the path", "[stopwords]") {
    bool threw = false;
    try {
        mse::StopWords::load("data/no_such_stopword_file.txt");
    } catch (const std::exception& error) {
        threw = std::string(error.what()).find("no_such_stopword_file") !=
                std::string::npos;
    }
    CHECK(threw);
}
