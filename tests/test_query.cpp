// test_query.cpp -- query tests on the golden4 fixture (word / phrase /
// boolean, the four thresholds, stop words, stemming, --pretty lookup).
// Run via `ctest --test-dir build`.

#include "query.hpp"
#include "stopwords.hpp"

#include <cstdint>
#include <initializer_list>
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

std::vector<std::uint32_t> docIDs(const mse::QueryResult& result) {
    std::vector<std::uint32_t> ids;
    for (const mse::QueryHit& hit : result.hits) {
        ids.push_back(hit.docID);
    }
    return ids;
}

bool docsAre(const mse::QueryResult& result,
             std::initializer_list<std::uint32_t> expected) {
    return docIDs(result) == std::vector<std::uint32_t>(expected);
}

}  // namespace

int main() {
    const mse::Corpus corpus =
        mse::Corpus::load("tests/fixtures/golden4/data/manifest.tsv");

    // -- the raw index (no stop words, no stemming) -------------------------
    mse::SplayDictionary splay;
    const mse::Index raw = mse::buildIndex(corpus, mse::BuildOptions{}, splay);

    mse::QueryOptions word;
    word.mode = mse::QueryMode::Word;
    report("word: 'silver' -> {3,1} (score order: tf 2 before tf 1)",
           docsAre(mse::runQuery(raw, "silver", word, nullptr), {3, 1}));
    report("word: case/punctuation normalized ('Silver!')",
           docsAre(mse::runQuery(raw, "Silver!", word, nullptr), {3, 1}));
    report("word: unknown term -> empty result",
           mse::runQuery(raw, "zzz", word, nullptr).hits.empty());
    report("word: multi-token input rejected with a note",
           mse::runQuery(raw, "hello world", word, nullptr).hits.empty());

    mse::QueryOptions phrase;
    phrase.mode = mse::QueryMode::Phrase;
    report("phrase: 'silver truck' -> {3,1}",
           docsAre(mse::runQuery(raw, "silver truck", phrase, nullptr), {3, 1}));
    report("phrase: 'gold damaged' -> {2}",
           docsAre(mse::runQuery(raw, "gold damaged", phrase, nullptr), {2}));
    report("phrase: 'gold truck' -> empty (not adjacent anywhere)",
           mse::runQuery(raw, "gold truck", phrase, nullptr).hits.empty());

    mse::QueryOptions both;
    both.mode = mse::QueryMode::Boolean;
    report("boolean: 'gold silver' default AND -> {1}",
           docsAre(mse::runQuery(raw, "gold silver", both, nullptr), {1}));
    both.minMatch = 2;
    report("boolean: --min-match 2 -> {1}",
           docsAre(mse::runQuery(raw, "gold silver", both, nullptr), {1}));
    both.minMatch = 1;
    report("boolean: --min-match 1 (OR) -> {1,3,2,4}",
           docsAre(mse::runQuery(raw, "gold silver", both, nullptr), {1, 3, 2, 4}));

    // -- the four thresholds ------------------------------------------------
    mse::QueryOptions capped = word;
    capped.maxDfRatio = 0.5;  // truck: df 3/4 = 0.75 > 0.5 -> dropped
    {
        const mse::QueryResult result = mse::runQuery(raw, "truck", capped, nullptr);
        report("--max-df: 'truck' (ratio 0.75) dropped, empty + note",
               result.hits.empty() && !result.notes.empty());
    }
    mse::QueryOptions rare = word;
    rare.minDf = 2;  // fire: df 1 -> dropped
    {
        const mse::QueryResult result = mse::runQuery(raw, "fire", rare, nullptr);
        report("--min-df: 'fire' (df 1) dropped, empty + note",
               result.hits.empty() && !result.notes.empty());
    }
    mse::QueryOptions topOne = both;
    topOne.topX = 1;
    report("--top-x: OR query capped to the best document -> {1}",
           docsAre(mse::runQuery(raw, "gold silver", topOne, nullptr), {1}));

    // -- stop words ---------------------------------------------------------
    report("raw index: stop word 'of' is still searchable -> {2,3,4}",
           docsAre(mse::runQuery(raw, "of", word, nullptr), {2, 3, 4}));

    const mse::StopWords stops = mse::StopWords::load("data/stopwords.txt");
    mse::BuildOptions filtered;
    filtered.stopWords = &stops;
    mse::SplayDictionary splay2;
    const mse::Index clean = mse::buildIndex(corpus, filtered, splay2);
    {
        const mse::QueryResult result = mse::runQuery(clean, "of", word, &stops);
        report("filtered index: 'of' reported as stop word, empty result",
               result.hits.empty() && !result.notes.empty());
    }
    report("filtered index: phrase 'in a fire' degenerates to the surviving "
           "term -> {2}",
           docsAre(mse::runQuery(clean, "in a fire", phrase, &stops), {2}));

    // -- stemming -----------------------------------------------------------
    mse::BuildOptions stemmed;
    stemmed.stem = true;
    mse::SplayDictionary splay3;
    const mse::Index stemIndex = mse::buildIndex(corpus, stemmed, splay3);
    report("raw index: 'arrive' not found (only 'arrived' occurs)",
           mse::runQuery(raw, "arrive", word, nullptr).hits.empty());
    report("stemmed index: 'arrive' matches docs {3,4}",
           docsAre(mse::runQuery(stemIndex, "arrive", word, nullptr), {3, 4}));
    report("stemmed index: 'arrived' matches the same stem",
           docsAre(mse::runQuery(stemIndex, "arrived", word, nullptr), {3, 4}));

    // -- --pretty position lookup ------------------------------------------
    {
        const mse::IndexTerm* silver = mse::findIndexTerm(raw, "silver");
        const mse::Posting& posting = silver->postings[1];  // doc 3
        const std::string line = mse::lineOfPosition(
            corpus.byID(3), raw, nullptr, silver->positions[posting.posStart]);
        report("pretty: raw index locates doc 3's source line",
               line == "Delivery of silver arrived in a silver truck");
    }
    {
        const mse::IndexTerm* cleanSilver = mse::findIndexTerm(clean, "silver");
        const mse::Posting& posting = cleanSilver->postings[1];  // doc 3
        const std::string line = mse::lineOfPosition(
            corpus.byID(3), clean, &stops, cleanSilver->positions[posting.posStart]);
        report("pretty: filtered index locates the same line (renumbered position)",
               line == "Delivery of silver arrived in a silver truck");
    }

    if (failures == 0) {
        std::cout << "\nall query tests passed\n";
        return 0;
    }
    std::cout << "\n" << failures << " test(s) failed\n";
    return 1;
}
