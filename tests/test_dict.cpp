// test_dict.cpp -- unit tests for the dictionary layer (splay + hash).
//
// Both implementations are checked against the same behavioural script, then
// against each other on a mixed insert/lookup workload: their vocabularies
// must agree exactly. Run via `ctest --test-dir build`.

#include "dict.hpp"

#include <algorithm>
#include <cstdint>
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

std::vector<std::string> termsOf(const std::vector<mse::TermEntry*>& entries) {
    std::vector<std::string> terms;
    terms.reserve(entries.size());
    for (const mse::TermEntry* entry : entries) {
        terms.push_back(entry->term);
    }
    return terms;
}

// The same script runs on both dictionaries; each observation is reported.
void behaviourScript(mse::Dictionary& dict, const std::string& label) {
    report(label + ": starts empty", dict.size() == 0 && dict.find("alpha") == nullptr);

    mse::TermEntry& first = dict.upsert("alpha");
    mse::TermEntry& again = dict.upsert("alpha");
    report(label + ": duplicate upsert returns the same entry",
           &first == &again && dict.size() == 1);

    first.postings.append(1, 0);
    first.postings.append(1, 3);
    first.postings.append(2, 0);
    report(label + ": postings accumulate df/tf/positions",
           first.postings.df() == 2 && first.postings.tf() == 3 &&
               first.postings.positions().size() == 3);

    // Entry references must survive later insertions (node stability).
    for (int i = 0; i < 300; ++i) {
        dict.upsert("word" + std::to_string(i));
    }
    report(label + ": entry references survive growth",
           &dict.upsert("alpha") == &first && dict.size() == 301);
    report(label + ": find() locates old and new terms",
           dict.find("alpha") == &first && dict.find("word299") != nullptr);
    report(label + ": find() returns nullptr for missing terms",
           dict.find("not-there") == nullptr);

    const std::vector<mse::TermEntry*> sorted = dict.sortedEntries();
    const std::vector<std::string> terms = termsOf(sorted);
    report(label + ": sortedEntries() returns every term once",
           terms.size() == dict.size());
    report(label + ": sortedEntries() is lexicographically sorted",
           std::is_sorted(terms.begin(), terms.end()));
}

// Deterministic pseudo-random workload, applied to both dictionaries at the
// same time; the resulting vocabularies must be identical.
void crossCheck(std::size_t vocabulary) {
    mse::SplayDictionary splay;
    mse::HashDictionary hash;

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

    const std::vector<std::string> splayTerms = termsOf(splay.sortedEntries());
    const std::vector<std::string> hashTerms = termsOf(hash.sortedEntries());
    report("cross-check: splay and hash agree on the vocabulary",
           splayTerms == hashTerms && splay.size() == hash.size());
    report("cross-check: both vocabularies are complete",
           hash.size() == vocabulary);
}

}  // namespace

int main() {
    mse::SplayDictionary splay;
    behaviourScript(splay, "splay");

    mse::HashDictionary hash;
    behaviourScript(hash, "hash");

    // Structure-specific observations.
    mse::SplayDictionary rootProbe;
    rootProbe.upsert("mid");
    rootProbe.upsert("aaa");
    rootProbe.upsert("zzz");
    report("splay: last accessed term is the root",
           rootProbe.rootTermForTesting() == "zzz");
    report("splay: rotations were performed", rootProbe.stats().rotations > 0);

    mse::HashDictionary rehashProbe;
    for (int i = 0; i < 300; ++i) {
        rehashProbe.upsert("r" + std::to_string(i));
    }
    report("hash: table rehashed while growing", rehashProbe.stats().rehashes > 0);

    crossCheck(300);

    if (failures == 0) {
        std::cout << "\nall dictionary tests passed\n";
        return 0;
    }
    std::cout << "\n" << failures << " test(s) failed\n";
    return 1;
}
