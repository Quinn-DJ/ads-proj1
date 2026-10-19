// test_dict.cpp -- unit tests for the dictionary layer (splay + hash).
//
// Both implementations are checked against the same behavioural script, then
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

// The same behavioural script runs on both dictionaries.
//
// It checks the contract the index builder relies on: missing lookups return
// nullptr, upsert is idempotent and returns a stable reference, posting
// updates survive further insertions, sortedEntries is sorted and complete,
// and size() is consistent. Running literally the same assertions on splay and
// hash is what makes their later byte-identical index output meaningful.
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

// Primary structure: the splay tree must satisfy the same interface contract
// as the hash baseline, including reference stability across 300 insertions.
TEST_CASE("the behavioural script passes on the splay dictionary", "[dict]") {
    mse::SplayDictionary splay;
    behaviourScript(splay, "splay");
}

// Baseline/cross-check structure: same script, so any behavioral drift between
// the two implementations is caught before the index build is compared.
TEST_CASE("the behavioural script passes on the hash dictionary", "[dict]") {
    mse::HashDictionary hash;
    behaviourScript(hash, "hash");
}

// Verify the self-adjusting property directly: after accessing "zzz" it must
// be at the root, and the rotations counter must have moved (proving the
// structure is actually splaying rather than behaving like a plain BST).
TEST_CASE("splay: last accessed term is the root, rotations happen", "[dict][splay]") {
    mse::SplayDictionary rootProbe;
    rootProbe.upsert("mid");
    rootProbe.upsert("aaa");
    rootProbe.upsert("zzz");
    CHECK(rootProbe.rootTermForTesting() == "zzz");
    CHECK(rootProbe.stats().rotations > 0);
}

// Growing past the 0.75 load factor must trigger at least one rehash; the
// counter is part of the dictionary comparison statistics in the report.
TEST_CASE("hash: table rehashes while growing", "[dict][hash]") {
    mse::HashDictionary rehashProbe;
    for (int i = 0; i < 300; ++i) {
        rehashProbe.upsert("r" + std::to_string(i));
    }
    CHECK(rehashProbe.stats().rehashes > 0);
}

// Cross-implementation check on a larger mixed workload (300 terms, 20k
// upserts, deterministic LCG). Both dictionaries must end with exactly the
// same sorted vocabulary and size. This is the dictionary-level counterpart of
// the byte-identical index-file test in test_index.cpp.
TEST_CASE("cross-check: splay and hash agree on a random workload", "[dict]") {
    const std::size_t vocabulary = 300;
    mse::SplayDictionary splay;
    mse::HashDictionary hash;

    // Deterministic LCG so the workload is random-looking but reproducible;
    // a real random source would make a failure impossible to replay.
    std::uint32_t state = 12345;
    const auto next = [&state]() {
        state = state * 1664525u + 1013904223u;
        return state;
    };
    for (int i = 0; i < 20000; ++i) {
        const std::string term = "w" + std::to_string(next() % vocabulary);
        splay.upsert(term).postings.append(1, static_cast<std::uint32_t>(i));
        hash.upsert(term).postings.append(1, static_cast<std::uint32_t>(i));
    }

    // The two independent structures saw the same operation stream, so any
    // divergence in vocabulary, size, or ordering is a real implementation bug.
    const std::vector<std::string> splayTerms = termsOf(splay.sortedEntries());
    const std::vector<std::string> hashTerms = termsOf(hash.sortedEntries());
    CHECK(splayTerms == hashTerms);
    CHECK(splay.size() == hash.size());
    CHECK(hash.size() == vocabulary);
}
