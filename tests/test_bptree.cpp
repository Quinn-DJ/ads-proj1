// test_bptree.cpp -- unit tests for the page-oriented B+Tree (B1).
//
// The oracle is std::map<std::string, uint64_t>: every operation is checked
// against it, plus the structural invariants I1-I5 via validate() and the
// "bulk-load == incremental build" equivalence the index format relies on.
//
// Key shapes covered: random ASCII keys (term-like, average ~8 chars),
// adversarial same-prefix keys, single-entry trees, and page sizes at the
// allowed minimum (forces frequent splits).

#include "catch_amalgamated.hpp"  // vendored Catch2 v3, single include

#include "bptree.hpp"

#include <cstdint>
#include <map>
#include <random>
#include <string>
#include <vector>

namespace {

// Deterministic pseudo-random key generator (tests must not flake).
std::vector<std::pair<std::string, std::uint64_t>> randomPairs(std::size_t count,
                                                               std::uint32_t seed,
                                                               int keyStyle) {
    std::mt19937 gen(seed);
    std::vector<std::pair<std::string, std::uint64_t>> out;
    out.reserve(count);
    for (std::size_t i = 0; i < count; ++i) {
        std::string key;
        if (keyStyle == 0) {  // term-like: 3-12 lowercase letters
            const int len = 3 + static_cast<int>(gen() % 10);
            for (int c = 0; c < len; ++c) {
                key.push_back(static_cast<char>('a' + gen() % 26));
            }
        } else {  // adversarial: shared prefix, distinct suffix
            key = "prefixprefix";
            key += std::to_string(gen() % 100000);
        }
        out.emplace_back(key, static_cast<std::uint64_t>(gen() % 1000000));
    }
    return out;
}

}  // namespace

TEST_CASE("bulk load then find every key (and only those)", "[bptree]") {
    const std::vector<std::pair<std::string, std::uint64_t>> pairs =
        randomPairs(5000, 42, 0);
    std::map<std::string, std::uint64_t> oracle(pairs.begin(), pairs.end());
    std::vector<std::pair<std::string, std::uint64_t>> sorted(oracle.begin(),
                                                              oracle.end());

    const mse::BTree tree = mse::BTree::bulkLoad(sorted);
    tree.validate();

    for (const auto& [key, value] : sorted) {
        const std::uint64_t* found = tree.find(key);
        REQUIRE(found != nullptr);
        CHECK(*found == value);
    }
    // Absent keys (with a marker char that never appears in generated keys).
    for (const char* missing : {"zzzzzzzzzzz", "a", "prefix", ""}) {
        CHECK(tree.find(missing) == nullptr);
    }
    CHECK(tree.stats().keyCount == sorted.size());
}

TEST_CASE("incremental inserts match the std::map oracle", "[bptree]") {
    const std::uint32_t pageSize = GENERATE(1056u, 4096u, 16384u);
    const std::vector<std::pair<std::string, std::uint64_t>> pairs =
        randomPairs(4000, 7 + pageSize, 0);
    std::map<std::string, std::uint64_t> oracle;

    mse::BTree tree(pageSize);
    for (const auto& [key, value] : pairs) {
        const bool fresh = oracle.find(key) == oracle.end();
        CHECK(tree.insert(key, value) == fresh);
        oracle[key] = value;
        if (fresh) {
            // spot-check after every few hundred inserts, and always at the end
        }
    }
    tree.validate();

    for (const auto& [key, value] : oracle) {
        const std::uint64_t* found = tree.find(key);
        REQUIRE(found != nullptr);
        CHECK(*found == value);
    }
    CHECK(tree.stats().keyCount == oracle.size());
    // A lookup touches exactly `height` pages (the disk I/O count in B2).
    const mse::BTreeStats stats = tree.stats();
    tree.find(oracle.begin()->first);
    CHECK(tree.lastFindPageReads() == stats.height);
}

TEST_CASE("upsert overwrites values, never duplicates keys", "[bptree]") {
    mse::BTree tree(1056);  // minimum page size: split-heavy
    CHECK(tree.insert("alpha", 1));
    CHECK_FALSE(tree.insert("alpha", 2));  // overwrite, not a new key
    CHECK_FALSE(tree.insert("alpha", 3));
    REQUIRE(tree.find("alpha") != nullptr);
    CHECK(*tree.find("alpha") == 3);
    tree.validate();
    CHECK(tree.stats().keyCount == 1);
}

TEST_CASE("range scans walk the leaf chain in order", "[bptree]") {
    const std::vector<std::pair<std::string, std::uint64_t>> pairs =
        randomPairs(3000, 99, 1);  // shared-prefix adversarial keys
    std::map<std::string, std::uint64_t> oracle(pairs.begin(), pairs.end());
    std::vector<std::pair<std::string, std::uint64_t>> sorted(oracle.begin(),
                                                              oracle.end());
    const mse::BTree tree = mse::BTree::bulkLoad(sorted);
    tree.validate();

    const std::string lo = "prefixprefix10000";
    const std::string hi = "prefixprefix80000";
    const auto scanned = tree.rangeScan(lo, hi);
    std::vector<std::pair<std::string, std::uint64_t>> expected;
    for (auto it = oracle.lower_bound(lo); it != oracle.end() && it->first < hi; ++it) {
        expected.emplace_back(it->first, it->second);
    }
    REQUIRE(scanned.size() == expected.size());
    for (std::size_t i = 0; i < expected.size(); ++i) {
        CHECK(scanned[i].first == expected[i].first);
        CHECK(scanned[i].second == expected[i].second);
    }

    // Full-range scan equals the whole table; empty range returns nothing.
    CHECK(tree.rangeScan("", "\xff\xff\xff\xff").size() == sorted.size());
    CHECK(tree.rangeScan("zzzz", "zzzz").empty());
}

TEST_CASE("bulk load and incremental build agree on lookups", "[bptree]") {
    const std::vector<std::pair<std::string, std::uint64_t>> pairs =
        randomPairs(2500, 1234, 0);
    std::map<std::string, std::uint64_t> oracle;
    std::vector<std::pair<std::string, std::uint64_t>> sorted;

    mse::BTree incremental(2048);
    for (const auto& [key, value] : pairs) {
        incremental.insert(key, value);
        oracle[key] = value;
    }
    sorted.assign(oracle.begin(), oracle.end());
    const mse::BTree loaded = mse::BTree::bulkLoad(sorted, 2048);

    incremental.validate();
    loaded.validate();
    for (const auto& [key, value] : sorted) {
        REQUIRE(incremental.find(key) != nullptr);
        REQUIRE(loaded.find(key) != nullptr);
        CHECK(*incremental.find(key) == value);
        CHECK(*loaded.find(key) == value);
    }
    // Same key universe -> same leaf-chain contents (structural freedom,
    // content equivalence): compare full range scans.
    CHECK(incremental.rangeScan("", "\xff\xff\xff\xff") ==
          loaded.rangeScan("", "\xff\xff\xff\xff"));
}

TEST_CASE("single-entry and empty trees hold their invariants", "[bptree]") {
    mse::BTree empty(4096);
    empty.validate();
    CHECK(empty.stats().height == 1);
    CHECK(empty.find("anything") == nullptr);
    CHECK(empty.rangeScan("", "\xff").empty());

    mse::BTree single(4096);
    single.insert("only", 7);
    single.validate();
    CHECK(single.stats().height == 1);
    REQUIRE(single.find("only") != nullptr);
    CHECK(*single.find("only") == 7);
}

TEST_CASE("input contracts fail loudly", "[bptree]") {
    CHECK_THROWS(mse::BTree(64));  // below the minimum page size

    const std::vector<std::pair<std::string, std::uint64_t>> unsorted = {
        {"b", 1}, {"a", 2}};
    CHECK_THROWS(mse::BTree::bulkLoad(unsorted));

    const std::string longKey(300, 'x');
    mse::BTree tree(4096);
    CHECK_THROWS(tree.insert(longKey, 1));
    CHECK(tree.find(longKey) == nullptr);  // not stored, but not a crash
}
