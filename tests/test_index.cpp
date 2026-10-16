// test_index.cpp -- build / serialize / load tests on the four-document
// example from the class03 notes (§1.2), plus a determinism check between
// the splay and hash dictionaries. Run via `ctest --test-dir build`
// (working directory = project root).

#include "index.hpp"
#include "stopwords.hpp"

#include <cstdint>
#include <fstream>
#include <iostream>
#include <iterator>
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

std::vector<std::uint8_t> readFile(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    return std::vector<std::uint8_t>((std::istreambuf_iterator<char>(in)),
                                     std::istreambuf_iterator<char>());
}

const mse::IndexTerm* findTerm(const mse::Index& index, const std::string& term) {
    for (const mse::IndexTerm& candidate : index.terms) {
        if (candidate.term == term) {
            return &candidate;
        }
    }
    return nullptr;
}

struct ExpectedPosting {
    std::uint32_t docID;
    std::uint32_t tf;
};

// Compare one term against the posting list printed in the lecture notes.
void checkTerm(const mse::Index& index, const std::string& term,
               const std::vector<ExpectedPosting>& expected,
               const std::string& label) {
    const mse::IndexTerm* found = findTerm(index, term);
    if (found == nullptr) {
        report(label + ": term '" + term + "' exists", false);
        return;
    }
    bool ok = found->postings.size() == expected.size();
    for (std::size_t i = 0; ok && i < expected.size(); ++i) {
        ok = found->postings[i].docID == expected[i].docID &&
             found->postings[i].tf == expected[i].tf;
    }
    report(label + ": '" + term + "' matches the lecture posting table", ok);
}

std::vector<std::uint32_t> positionsOf(const mse::IndexTerm& term,
                                       std::size_t postingIndex) {
    const mse::Posting& posting = term.postings[postingIndex];
    return std::vector<std::uint32_t>(
        term.positions.begin() + posting.posStart,
        term.positions.begin() + posting.posStart + posting.tf);
}

bool sameIndex(const mse::Index& a, const mse::Index& b) {
    if (a.flags != b.flags || a.tokenCount != b.tokenCount) {
        return false;
    }
    if (a.docs.size() != b.docs.size() || a.terms.size() != b.terms.size()) {
        return false;
    }
    for (std::size_t i = 0; i < a.docs.size(); ++i) {
        if (a.docs[i].docID != b.docs[i].docID ||
            a.docs[i].title != b.docs[i].title ||
            a.docs[i].keptTokens != b.docs[i].keptTokens) {
            return false;
        }
    }
    for (std::size_t i = 0; i < a.terms.size(); ++i) {
        const mse::IndexTerm& x = a.terms[i];
        const mse::IndexTerm& y = b.terms[i];
        if (x.term != y.term || x.positions != y.positions ||
            x.postings.size() != y.postings.size()) {
            return false;
        }
        for (std::size_t k = 0; k < x.postings.size(); ++k) {
            if (x.postings[k].docID != y.postings[k].docID ||
                x.postings[k].tf != y.postings[k].tf ||
                x.postings[k].posStart != y.postings[k].posStart) {
                return false;
            }
        }
    }
    return true;
}

}  // namespace

int main() {
    const mse::Corpus corpus =
        mse::Corpus::load("tests/fixtures/golden4/data/manifest.tsv");

    // 1. raw build (no stop words, no stemming) vs the lecture table.
    mse::SplayDictionary splay;
    const mse::Index raw = mse::buildIndex(corpus, mse::BuildOptions{}, splay);
    report("raw: four documents", raw.docs.size() == 4);
    report("raw: eleven distinct terms", raw.terms.size() == 11);
    report("raw: token count", raw.tokenCount == 3 + 7 + 8 + 7);
    // Discrepancy found by this test: the class03 note table prints
    // a -> (2,1) (4,1) (times 2), but the example *sentences* -- verbatim
    // from the notes, and confirmed byte-for-byte in the original lecture
    // .ppt ("arrived in a silver truck") -- place "a" in doc 3 as well.
    // The sentences are the ground truth for building an index, so the
    // expectation follows them; the difference is logged in notes.md.
    checkTerm(raw, "a", {{2, 1}, {3, 1}, {4, 1}}, "raw");
    checkTerm(raw, "arrived", {{3, 1}, {4, 1}}, "raw");
    checkTerm(raw, "damaged", {{2, 1}}, "raw");
    checkTerm(raw, "delivery", {{3, 1}}, "raw");
    checkTerm(raw, "fire", {{2, 1}}, "raw");
    checkTerm(raw, "gold", {{1, 1}, {2, 1}, {4, 1}}, "raw");
    checkTerm(raw, "of", {{2, 1}, {3, 1}, {4, 1}}, "raw");
    checkTerm(raw, "in", {{2, 1}, {3, 1}, {4, 1}}, "raw");
    checkTerm(raw, "shipment", {{2, 1}, {4, 1}}, "raw");
    checkTerm(raw, "silver", {{1, 1}, {3, 2}}, "raw");
    checkTerm(raw, "truck", {{1, 1}, {3, 1}, {4, 1}}, "raw");

    // Positions: doc 1 "Gold silver truck" -> silver at 1;
    // doc 3 "... silver ... silver ..." -> silver at 2 and 6.
    const mse::IndexTerm* silver = findTerm(raw, "silver");
    report("raw: position slices are correct",
           silver != nullptr &&
               positionsOf(*silver, 0) == std::vector<std::uint32_t>{1} &&
               positionsOf(*silver, 1) == std::vector<std::uint32_t>({2, 6}));

    // 2. determinism: the hash dictionary must give the identical index,
    // and the serialized files must match byte for byte.
    mse::HashDictionary hash;
    const mse::Index hashed = mse::buildIndex(corpus, mse::BuildOptions{}, hash);
    report("splay vs hash: identical Index objects", sameIndex(raw, hashed));
    mse::writeIndex("build/test_index_splay.bin", raw);
    mse::writeIndex("build/test_index_hash.bin", hashed);
    report("splay vs hash: byte-identical index files",
           readFile("build/test_index_splay.bin") ==
               readFile("build/test_index_hash.bin"));

    // 3. round trip: loadIndex reproduces the built index (and validates).
    const mse::Index loaded = mse::loadIndex("build/test_index_splay.bin");
    report("round trip: loadIndex reproduces the built index",
           sameIndex(raw, loaded));

    // 4. stop word variant: a/of/in disappear, positions are renumbered.
    const mse::StopWords stops = mse::StopWords::load("data/stopwords.txt");
    mse::BuildOptions filtered;
    filtered.stopWords = &stops;
    mse::SplayDictionary splay2;
    const mse::Index clean = mse::buildIndex(corpus, filtered, splay2);
    report("stopwords: eight terms remain", clean.terms.size() == 8);
    report("stopwords: 'a'/'of'/'in' are gone",
           findTerm(clean, "a") == nullptr && findTerm(clean, "of") == nullptr &&
               findTerm(clean, "in") == nullptr);
    const mse::IndexTerm* cleanSilver = findTerm(clean, "silver");
    report("stopwords: positions renumbered in the filtered stream",
           cleanSilver != nullptr &&
               positionsOf(*cleanSilver, 1) == std::vector<std::uint32_t>({1, 3}));
    report("stopwords: flag recorded", clean.flags == mse::kIndexFlagStopWords);

    // 5. a corrupt file must fail loudly.
    {
        std::ofstream out("build/test_index_bad.bin", std::ios::binary);
        out << "this is not an index";
    }
    bool threw = false;
    try {
        mse::loadIndex("build/test_index_bad.bin");
    } catch (const std::exception&) {
        threw = true;
    }
    report("corrupt file: loadIndex throws", threw);

    if (failures == 0) {
        std::cout << "\nall index tests passed\n";
        return 0;
    }
    std::cout << "\n" << failures << " test(s) failed\n";
    return 1;
}
