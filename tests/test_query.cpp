// test_query.cpp -- query tests on the golden4 fixture (word / phrase /
// boolean, the four thresholds, stop words, stemming, --pretty lookup).
// Run via `ctest --test-dir build`.

#include <cstdint>
#include <initializer_list>
#include <string>
#include <vector>

#include "catch_amalgamated.hpp"  // vendored Catch2 v3, single include
#include "query.hpp"
#include "stopwords.hpp"

namespace {

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

// Word-mode basics: case/punctuation normalization, score order (higher tf
// first, then docID), an unknown term, and the deliberate rejection of a
// multi-token --word query.
TEST_CASE("word queries: tf score order, normalization, misses", "[query]") {
  const mse::Corpus corpus =
      mse::Corpus::load("tests/fixtures/golden4/data/manifest.tsv");
  mse::SplayDictionary splay;
  const mse::Index raw = mse::buildIndex(corpus, mse::BuildOptions{}, splay);

  mse::QueryOptions word;
  word.mode = mse::QueryMode::Word;

  // Doc 3 contains silver twice (tf 2), doc 1 once (tf 1), so score order
  // puts doc 3 first even though its docID is larger.
  CHECK(docsAre(mse::runQuery(raw, "silver", word, nullptr),
                {3, 1}));  // tf 2 before tf 1
  CHECK(docsAre(mse::runQuery(raw, "Silver!", word, nullptr),
                {3, 1}));  // case/punctuation
  CHECK(mse::runQuery(raw, "zzz", word, nullptr).hits.empty());  // unknown term
  CHECK(mse::runQuery(raw, "hello world", word, nullptr)
            .hits.empty());  // multi-token rejected
}

// Phrase mode requires adjacency in order: "silver truck" matches, but
// "gold truck" does not because the words are not next to each other in any
// document. Positions come from the raw (unfiltered) index here.
TEST_CASE("phrase queries: adjacency on the filtered stream", "[query]") {
  const mse::Corpus corpus =
      mse::Corpus::load("tests/fixtures/golden4/data/manifest.tsv");
  mse::SplayDictionary splay;
  const mse::Index raw = mse::buildIndex(corpus, mse::BuildOptions{}, splay);

  mse::QueryOptions phrase;
  phrase.mode = mse::QueryMode::Phrase;

  CHECK(docsAre(mse::runQuery(raw, "silver truck", phrase, nullptr), {3, 1}));
  CHECK(docsAre(mse::runQuery(raw, "gold damaged", phrase, nullptr), {2}));
  // Both words occur in doc 4, but not next to each other, so adjacency
  // (not mere co-occurrence) is what the phrase mode checks.
  CHECK(mse::runQuery(raw, "gold truck", phrase, nullptr)
            .hits.empty());  // not adjacent
}

// Boolean mode: default minMatch=0 means AND; k=1 means OR. The OR result
// order also checks the matched-then-tf-then-docID scoring rule.
TEST_CASE("boolean queries: min-match semantics and score order", "[query]") {
  const mse::Corpus corpus =
      mse::Corpus::load("tests/fixtures/golden4/data/manifest.tsv");
  mse::SplayDictionary splay;
  const mse::Index raw = mse::buildIndex(corpus, mse::BuildOptions{}, splay);

  mse::QueryOptions both;
  both.mode = mse::QueryMode::Boolean;
  CHECK(docsAre(mse::runQuery(raw, "gold silver", both, nullptr),
                {1}));  // default AND

  both.minMatch = 2;
  CHECK(docsAre(mse::runQuery(raw, "gold silver", both, nullptr), {1}));

  // OR order: doc 1 matches both terms, then the one-term documents by tf
  // and docID (doc 3 has two silvers and therefore ranks before doc 2/4).
  both.minMatch = 1;
  CHECK(docsAre(mse::runQuery(raw, "gold silver", both, nullptr),
                {1, 3, 2, 4}));  // OR
}

// Threshold semantics: --max-df drops a common term (truck), --min-df drops a
// hapax (fire), and --top-x caps the ranked list. Each is checked on a fixture
// where the expected outcome follows directly from the posting table.
TEST_CASE("the four query thresholds drop and cap as documented", "[query]") {
  const mse::Corpus corpus =
      mse::Corpus::load("tests/fixtures/golden4/data/manifest.tsv");
  mse::SplayDictionary splay;
  const mse::Index raw = mse::buildIndex(corpus, mse::BuildOptions{}, splay);

  mse::QueryOptions word;
  word.mode = mse::QueryMode::Word;

  // --max-df: truck: df 3/4 = 0.75 > 0.5 -> dropped
  mse::QueryOptions capped = word;
  capped.maxDfRatio = 0.5;
  {
    const mse::QueryResult result =
        mse::runQuery(raw, "truck", capped, nullptr);
    CHECK(result.hits.empty());
    CHECK_FALSE(result.notes.empty());
  }

  // --min-df: fire: df 1 -> dropped
  mse::QueryOptions rare = word;
  rare.minDf = 2;
  {
    const mse::QueryResult result = mse::runQuery(raw, "fire", rare, nullptr);
    CHECK(result.hits.empty());
    CHECK_FALSE(result.notes.empty());
  }

  // --top-x: OR query capped to the best document
  mse::QueryOptions topOne;
  topOne.mode = mse::QueryMode::Boolean;
  topOne.topX = 1;
  CHECK(docsAre(mse::runQuery(raw, "gold silver", topOne, nullptr), {1}));
}

// The same query behaves differently depending on the build flags: on a raw
// index "of" is searchable; on a filtered index it returns an empty set plus a
// note. The phrase "in a fire" on the filtered index degenerates to "fire",
// which is the documented phrase-on-filtered-stream limitation.
TEST_CASE(
    "stop words are searchable on raw indexes, explained on filtered ones",
    "[query]") {
  const mse::Corpus corpus =
      mse::Corpus::load("tests/fixtures/golden4/data/manifest.tsv");
  mse::SplayDictionary splay;
  const mse::Index raw = mse::buildIndex(corpus, mse::BuildOptions{}, splay);

  mse::QueryOptions word;
  word.mode = mse::QueryMode::Word;
  mse::QueryOptions phrase;
  phrase.mode = mse::QueryMode::Phrase;

  CHECK(docsAre(mse::runQuery(raw, "of", word, nullptr), {2, 3, 4}));

  const mse::StopWords stops = mse::StopWords::load("data/stopwords.txt");
  mse::BuildOptions filtered;
  filtered.stopWords = &stops;
  mse::SplayDictionary splay2;
  const mse::Index clean = mse::buildIndex(corpus, filtered, splay2);
  {
    const mse::QueryResult result = mse::runQuery(clean, "of", word, &stops);
    CHECK(result.hits.empty());
    CHECK_FALSE(result.notes.empty());
  }
  // The phrase degenerates to the surviving term.
  CHECK(docsAre(mse::runQuery(clean, "in a fire", phrase, &stops), {2}));
}

// Index/query consistency: "arrive" is absent from the raw index (only
// "arrived" occurs), but after stemming both surface forms resolve to the same
// stem. This is the behavior that would break if the query side forgot to stem.
TEST_CASE("stemming makes surface forms find their stem", "[query]") {
  const mse::Corpus corpus =
      mse::Corpus::load("tests/fixtures/golden4/data/manifest.tsv");
  mse::SplayDictionary splay;
  const mse::Index raw = mse::buildIndex(corpus, mse::BuildOptions{}, splay);

  mse::BuildOptions stemmed;
  stemmed.stem = true;
  mse::SplayDictionary splay3;
  const mse::Index stemIndex = mse::buildIndex(corpus, stemmed, splay3);

  mse::QueryOptions word;
  word.mode = mse::QueryMode::Word;

  CHECK(mse::runQuery(raw, "arrive", word, nullptr)
            .hits.empty());  // only 'arrived' occurs
  CHECK(docsAre(mse::runQuery(stemIndex, "arrive", word, nullptr), {3, 4}));
  CHECK(docsAre(mse::runQuery(stemIndex, "arrived", word, nullptr), {3, 4}));
}

// --pretty support: lineOfPosition() must recover the exact source line for a
// stored filtered-stream position, both on a raw index and on a stop-filtered
// index (where positions were renumbered). A wrong stop list would make the
// second case fail, which is why the CLI warns when it is missing.
TEST_CASE("--pretty locates the source line of a hit position", "[query]") {
  const mse::Corpus corpus =
      mse::Corpus::load("tests/fixtures/golden4/data/manifest.tsv");
  mse::SplayDictionary splay;
  const mse::Index raw = mse::buildIndex(corpus, mse::BuildOptions{}, splay);

  {
    const mse::IndexTerm* silver = mse::findIndexTerm(raw, "silver");
    REQUIRE(silver != nullptr);
    const mse::Posting& posting = silver->postings[1];  // doc 3
    const std::string line = mse::lineOfPosition(
        corpus.byID(3), raw, nullptr, silver->positions[posting.posStart]);
    CHECK(line == "Delivery of silver arrived in a silver truck");
  }
  {
    const mse::StopWords stops = mse::StopWords::load("data/stopwords.txt");
    mse::BuildOptions filtered;
    filtered.stopWords = &stops;
    mse::SplayDictionary splay2;
    const mse::Index clean = mse::buildIndex(corpus, filtered, splay2);

    const mse::IndexTerm* cleanSilver = mse::findIndexTerm(clean, "silver");
    REQUIRE(cleanSilver != nullptr);
    const mse::Posting& posting = cleanSilver->postings[1];  // doc 3
    const std::string line = mse::lineOfPosition(
        corpus.byID(3), clean, &stops,
        cleanSilver->positions[posting.posStart]);  // renumbered position
    CHECK(line == "Delivery of silver arrived in a silver truck");
  }
}
