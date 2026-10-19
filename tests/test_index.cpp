// test_index.cpp -- build / serialize / load tests on the four-document
// example from the class03 notes (§1.2), plus a determinism check between
// the splay and hash dictionaries. Run via `ctest --test-dir build`
// (working directory = project root).

#include "catch_amalgamated.hpp"  // vendored Catch2 v3, single include

#include "blockbuild.hpp"
#include "index.hpp"
#include "stopwords.hpp"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace {

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
    INFO(label << ": term '" << term << "'");
    const mse::IndexTerm* found = findTerm(index, term);
    REQUIRE(found != nullptr);

    std::vector<std::uint32_t> gotIDs;
    std::vector<std::uint32_t> gotTfs;
    for (const mse::Posting& posting : found->postings) {
        gotIDs.push_back(posting.docID);
        gotTfs.push_back(posting.tf);
    }
    std::vector<std::uint32_t> wantIDs;
    std::vector<std::uint32_t> wantTfs;
    for (const ExpectedPosting& posting : expected) {
        wantIDs.push_back(posting.docID);
        wantTfs.push_back(posting.tf);
    }
    CHECK(gotIDs == wantIDs);
    CHECK(gotTfs == wantTfs);
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

TEST_CASE("raw build reproduces the lecture posting table", "[index][golden]") {
    const mse::Corpus corpus =
        mse::Corpus::load("tests/fixtures/golden4/data/manifest.tsv");

    mse::SplayDictionary splay;
    const mse::Index raw = mse::buildIndex(corpus, mse::BuildOptions{}, splay);

    CHECK(raw.docs.size() == 4);
    CHECK(raw.terms.size() == 11);
    CHECK(raw.tokenCount == 3 + 7 + 8 + 7);

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
    REQUIRE(silver != nullptr);
    CHECK(positionsOf(*silver, 0) == std::vector<std::uint32_t>{1});
    CHECK(positionsOf(*silver, 1) == std::vector<std::uint32_t>({2, 6}));
}

TEST_CASE("splay and hash dictionaries build identical indexes", "[index]") {
    const mse::Corpus corpus =
        mse::Corpus::load("tests/fixtures/golden4/data/manifest.tsv");

    mse::SplayDictionary splay;
    const mse::Index raw = mse::buildIndex(corpus, mse::BuildOptions{}, splay);
    mse::HashDictionary hash;
    const mse::Index hashed = mse::buildIndex(corpus, mse::BuildOptions{}, hash);

    CHECK(sameIndex(raw, hashed));
    mse::writeIndex("build/test_index_splay.bin", raw);
    mse::writeIndex("build/test_index_hash.bin", hashed);
    CHECK(readFile("build/test_index_splay.bin") ==
          readFile("build/test_index_hash.bin"));

    // Round trip: loadIndex reproduces the built index (and validates).
    const mse::Index loaded = mse::loadIndex("build/test_index_splay.bin");
    CHECK(sameIndex(raw, loaded));
}

TEST_CASE("stop word variant: terms vanish, positions renumber", "[index]") {
    const mse::Corpus corpus =
        mse::Corpus::load("tests/fixtures/golden4/data/manifest.tsv");

    const mse::StopWords stops = mse::StopWords::load("data/stopwords.txt");
    mse::BuildOptions filtered;
    filtered.stopWords = &stops;
    mse::SplayDictionary splay2;
    const mse::Index clean = mse::buildIndex(corpus, filtered, splay2);

    CHECK(clean.terms.size() == 8);
    CHECK(findTerm(clean, "a") == nullptr);
    CHECK(findTerm(clean, "of") == nullptr);
    CHECK(findTerm(clean, "in") == nullptr);

    const mse::IndexTerm* cleanSilver = findTerm(clean, "silver");
    REQUIRE(cleanSilver != nullptr);
    CHECK(positionsOf(*cleanSilver, 1) == std::vector<std::uint32_t>({1, 3}));
    CHECK(clean.flags == mse::kIndexFlagStopWords);
}

TEST_CASE("a corrupt index file must fail loudly", "[index]") {
    {
        std::ofstream out("build/test_index_bad.bin", std::ios::binary);
        out << "this is not an index";
    }
    CHECK_THROWS_AS(mse::loadIndex("build/test_index_bad.bin"), std::exception);
}

TEST_CASE("block-wise build equals the in-memory build byte for byte",
          "[index][blocks]") {
    const mse::Corpus corpus =
        mse::Corpus::load("tests/fixtures/golden4/data/manifest.tsv");

    // Reference: the ordinary single-pass build.
    mse::SplayDictionary splay;
    const mse::Index inMemory = mse::buildIndex(corpus, mse::BuildOptions{}, splay);
    mse::writeIndex("build/test_index_memory.bin", inMemory);

    // Block build with an absurdly small budget -- each golden4 document has
    // ~8 terms, so budget 2 spills after every document: 4 blocks, each term
    // split across as many blocks as documents it occurs in.
    mse::BlockBuildOptions options;
    options.blockTerms = 2;
    options.blockDir = "build/blockbuild_test";
    mse::BlockBuildStats stats;
    const mse::Index blocked = mse::buildIndexBlocked(corpus, options, stats);

    CHECK(blocked.flags == inMemory.flags);
    CHECK(blocked.tokenCount == inMemory.tokenCount);
    CHECK(blocked.docs.size() == inMemory.docs.size());
    CHECK(blocked.terms.size() == inMemory.terms.size());
    CHECK(stats.blocks >= 2);  // the budget must actually have forced spills
    CHECK(stats.maxLiveTerms >= 2);

    // Term tables identical (sorted, same postings and positions).
    for (std::size_t i = 0; i < inMemory.terms.size(); ++i) {
        CHECK(blocked.terms[i].term == inMemory.terms[i].term);
        CHECK(blocked.terms[i].postings == inMemory.terms[i].postings);
        CHECK(blocked.terms[i].positions == inMemory.terms[i].positions);
    }
    mse::validateIndex(blocked);
    mse::writeIndex("build/test_index_blocked.bin", blocked);
    CHECK(readFile("build/test_index_memory.bin") ==
          readFile("build/test_index_blocked.bin"));
}

TEST_CASE("varint round trips across every length class", "[index][varint]") {
    // Boundaries of the four payload classes (6/14/22/30 bits) plus a
    // spread inside each; encode then decode must return the same value
    // and consume exactly the encoded bytes.
    const std::vector<std::uint32_t> values = {
        0, 1, 5, 63, 64, 65, 100, 16383, 16384, 20000,
        (1u << 21) - 1, (1u << 21), (1u << 22) - 1, (1u << 22), 123456789,
        (1u << 30) - 1};
    for (const std::uint32_t value : values) {
        const std::string encoded = mse::encodeVarint(value);
        const std::uint8_t* cursor =
            reinterpret_cast<const std::uint8_t*>(encoded.data());
        const std::uint8_t* end = cursor + encoded.size();
        CHECK(mse::decodeVarint(cursor, end) == value);
        CHECK(cursor == end);  // consumed exactly the encoded bytes
    }
    // Values above 30 bits are rejected (they cannot occur in our format).
    CHECK_THROWS(mse::encodeVarint(1u << 30));
    // Truncated buffers fail loudly instead of reading garbage.
    const std::string four = mse::encodeVarint((1u << 22));
    const std::uint8_t* cursor =
        reinterpret_cast<const std::uint8_t*>(four.data());
    CHECK_THROWS(mse::decodeVarint(cursor, cursor + 2));
}

TEST_CASE("compressed v2 index round trips and shrinks", "[index][v2]") {
    const mse::Corpus corpus =
        mse::Corpus::load("tests/fixtures/golden4/data/manifest.tsv");
    mse::SplayDictionary splay;
    const mse::Index index = mse::buildIndex(corpus, mse::BuildOptions{}, splay);

    mse::writeIndex("build/test_index_v1.bin", index);
    mse::writeIndexCompressed("build/test_index_v2.bin", index);

    // Same content, both encodings.
    const mse::Index loadedV1 = mse::loadIndex("build/test_index_v1.bin");
    const mse::Index loadedV2 = mse::loadIndex("build/test_index_v2.bin");
    CHECK(sameIndex(index, loadedV1));
    CHECK(sameIndex(index, loadedV2));
    mse::validateIndex(loadedV2);

    // golden4 postings are small numbers -- gaps/deltas dominate.
    CHECK(std::filesystem::file_size("build/test_index_v2.bin") <
          std::filesystem::file_size("build/test_index_v1.bin"));

    // The disk dictionary works against the v2 file as well (its values
    // carry the compressed block ranges).
    mse::writeDictFile(index, "build/test_index_v2.bpt", 4096, /*compressed=*/true);
    const mse::LazyIndexReader lazy("build/test_index_v2.bin",
                                    "build/test_index_v2.bpt");
    for (const mse::IndexTerm& reference : index.terms) {
        mse::IndexTerm viaDict;
        REQUIRE(lazy.readTerm(reference.term, viaDict));
        CHECK(viaDict.postings.size() == reference.postings.size());
        CHECK(viaDict.positions == reference.positions);
    }
}

TEST_CASE("B+Tree sidecar serves every term without loading the table",
          "[index][dict]") {
    const mse::Corpus corpus =
        mse::Corpus::load("tests/fixtures/golden4/data/manifest.tsv");
    mse::SplayDictionary splay;
    const mse::Index index = mse::buildIndex(corpus, mse::BuildOptions{}, splay);

    mse::writeIndex("build/test_index_lazy.bin", index);
    mse::writeDictFile(index, "build/test_index_lazy.bpt");

    const mse::LazyIndexReader lazy("build/test_index_lazy.bin",
                                    "build/test_index_lazy.bpt");
    CHECK(lazy.docs().size() == index.docs.size());
    CHECK(lazy.termCount() == index.terms.size());
    CHECK(lazy.flags() == index.flags);
    CHECK(lazy.tokenCount() == index.tokenCount);

    // Every term resolves and parses identical to the loaded form.
    for (const mse::IndexTerm& reference : index.terms) {
        mse::IndexTerm viaDict;
        REQUIRE(lazy.readTerm(reference.term, viaDict));
        CHECK(viaDict.postings.size() == reference.postings.size());
        CHECK(viaDict.positions == reference.positions);
        for (std::size_t i = 0; i < reference.postings.size(); ++i) {
            CHECK(viaDict.postings[i].docID == reference.postings[i].docID);
            CHECK(viaDict.postings[i].tf == reference.postings[i].tf);
        }
    }
    // Absent terms report absent; the dictionary side is a real lookup.
    mse::IndexTerm missing;
    CHECK_FALSE(lazy.readTerm("zzzznotaterm", missing));
    CHECK(lazy.dictHeight() >= 1);

    // A dictionary from a DIFFERENT index must be rejected up front (the
    // term-count cross-check in the constructor).
    mse::SplayDictionary other;
    mse::Index shifted = index;
    shifted.terms.pop_back();  // one term fewer -> different term count
    mse::writeIndex("build/test_index_shifted.bin", shifted);
    mse::writeDictFile(shifted, "build/test_index_shifted.bpt");
    CHECK_THROWS_AS((mse::LazyIndexReader{"build/test_index_lazy.bin",
                                          "build/test_index_shifted.bpt"}),
                    std::exception);
}
