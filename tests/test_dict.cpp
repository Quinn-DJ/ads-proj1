// test_dict.cpp -- unit tests for the dictionary layer (splay + hash + map).
//
// All implementations are checked against the same behavioural script, then
// against each other on a mixed insert/lookup workload: their vocabularies
// must agree exactly. Run via `ctest --test-dir build`.

#include "catch_amalgamated.hpp"  // vendored Catch2 v3, single include

#include "dict.hpp"

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

namespace {

std::vector<std::string> termsOf(const std::vector<mse::TermEntry*>& entries) {
    std::vector<std::string> terms;
    terms.reserve(entries.size());
    for (const mse::TermEntry* entry : entries) {
        terms.push_back(entry->term);
    }
    return terms;
}

// The same script runs on both dictionaries; each observation is asserted
// with the label echoed into the failure message.
void behaviourScript(mse::Dictionary& dict, const std::string& label) {
    INFO("dictionary: " << label);
    CHECK(dict.size() == 0);
    CHECK(dict.find("alpha") == nullptr);

    mse::TermEntry& first = dict.upsert("alpha");
    mse::TermEntry& again = dict.upsert("alpha");
    CHECK(&first == &again);
    CHECK(dict.size() == 1);

    first.postings.append(1, 0);
    first.postings.append(1, 3);
    first.postings.append(2, 0);
    CHECK(first.postings.df() == 2);
    CHECK(first.postings.tf() == 3);
    CHECK(first.postings.positions().size() == 3);

    // Entry references must survive later insertions (node stability).
    for (int i = 0; i < 300; ++i) {
        dict.upsert("word" + std::to_string(i));
    }
    CHECK(&dict.upsert("alpha") == &first);
    CHECK(dict.size() == 301);
    CHECK(dict.find("alpha") == &first);
    CHECK(dict.find("word299") != nullptr);
    CHECK(dict.find("not-there") == nullptr);

    const std::vector<mse::TermEntry*> sorted = dict.sortedEntries();
    const std::vector<std::string> terms = termsOf(sorted);
    CHECK(terms.size() == dict.size());
    CHECK(std::is_sorted(terms.begin(), terms.end()));
}

}  // namespace

TEST_CASE("the behavioural script passes on the splay dictionary", "[dict]") {
    mse::SplayDictionary splay;
    behaviourScript(splay, "splay");
}

TEST_CASE("the behavioural script passes on the hash dictionary", "[dict]") {
    mse::HashDictionary hash;
    behaviourScript(hash, "hash");
}

TEST_CASE("the behavioural script passes on the std::map dictionary", "[dict]") {
    mse::MapDictionary map;
    behaviourScript(map, "map");
}

TEST_CASE("splay: last accessed term is the root, rotations happen", "[dict][splay]") {
    mse::SplayDictionary rootProbe;
    rootProbe.upsert("mid");
    rootProbe.upsert("aaa");
    rootProbe.upsert("zzz");
    CHECK(rootProbe.rootTermForTesting() == "zzz");
    CHECK(rootProbe.stats().rotations > 0);
}

TEST_CASE("hash: table rehashes while growing", "[dict][hash]") {
    mse::HashDictionary rehashProbe;
    for (int i = 0; i < 300; ++i) {
        rehashProbe.upsert("r" + std::to_string(i));
    }
    CHECK(rehashProbe.stats().rehashes > 0);
}

TEST_CASE("cross-check: splay and hash agree on a random workload", "[dict]") {
    const std::size_t vocabulary = 300;
    mse::SplayDictionary splay;
    mse::HashDictionary hash;
    mse::MapDictionary map;

    std::uint32_t state = 12345;
    const auto next = [&state]() {
        state = state * 1664525u + 1013904223u;
        return state;
    };
    for (int i = 0; i < 20000; ++i) {
        const std::string term = "w" + std::to_string(next() % vocabulary);
        splay.upsert(term).postings.append(1, static_cast<std::uint32_t>(i));
        hash.upsert(term).postings.append(1, static_cast<std::uint32_t>(i));
        map.upsert(term).postings.append(1, static_cast<std::uint32_t>(i));
    }

    const std::vector<std::string> splayTerms = termsOf(splay.sortedEntries());
    const std::vector<std::string> hashTerms = termsOf(hash.sortedEntries());
    const std::vector<std::string> mapTerms = termsOf(map.sortedEntries());
    CHECK(splayTerms == hashTerms);
    CHECK(splayTerms == mapTerms);
    CHECK(splay.size() == hash.size());
    CHECK(map.size() == vocabulary);
    CHECK(map.stats().stringComparisons > 0);  // comparator is instrumented
}
