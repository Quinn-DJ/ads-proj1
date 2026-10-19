// test_count.cpp -- unit tests for the word counter's entropy features and
// the derived stop-word rule (the entropy-based extension of plan.md 4.2).
//
// The entropy expectations are written as *formulas* over the golden4
// fixture's known token counts, so the test re-derives the spec (count.hpp
// header) instead of echoing implementation output.
//
// golden4 body lines and token counts (see fixtures/golden4/data/text/):
//   doc1 "Gold silver truck"                          L=3
//   doc2 "Shipment of gold damaged in a fire"         L=7
//   doc3 "Delivery of silver arrived in a silver truck" L=8
//   doc4 "Shipment of gold arrived in a truck"        L=7

#include "catch_amalgamated.hpp"  // vendored Catch2 v3, single include

#include "count.hpp"

#include <cmath>
#include <string>
#include <vector>

using Catch::Approx;

namespace {

// Shorthand: -sum q*log2(q) over the given weights (already normalized).
double entropyOf(const std::vector<double>& weights) {
    double h = 0.0;
    for (const double q : weights) {
        h -= q * std::log2(q);
    }
    return h;
}

const mse::TermRow* findRow(const mse::WordCount& count, const std::string& term) {
    for (const mse::TermRow& row : count.rows) {
        if (row.term == term) {
            return &row;
        }
    }
    return nullptr;
}

}  // namespace

TEST_CASE("entropy features on the golden4 fixture", "[count]") {
    const mse::Corpus corpus =
        mse::Corpus::load("tests/fixtures/golden4/data/manifest.tsv");
    const mse::WordCount count = mse::countWords(corpus, mse::CountOptions{});

    SECTION("'of' appears once in docs 2,3,4: uniform -> both norms 1.0") {
        const mse::TermRow* row = findRow(count, "of");
        REQUIRE(row != nullptr);
        REQUIRE(row->tf == 3);
        REQUIRE(row->df == 3);
        // raw p = (1/3, 1/3, 1/3): H = log2(3), normalized exactly 1
        CHECK(row->entropy == Approx(std::log2(3.0)).epsilon(1e-12));
        CHECK(row->entropyNorm == Approx(1.0).epsilon(1e-12));
        // rates (1/7, 1/8, 1/7) -> q = (8/23, 7/23, 8/23)
        const double expected = entropyOf({8.0 / 23.0, 7.0 / 23.0, 8.0 / 23.0}) /
                                std::log2(3.0);
        CHECK(row->entropyRateNorm == Approx(expected).epsilon(1e-12));
    }

    SECTION("'gold' is same-shape but doc1 is short -> rate norm below 1") {
        const mse::TermRow* row = findRow(count, "gold");
        REQUIRE(row != nullptr);
        // counts (1,1,1) in docs 1,2,4: raw norm is still exactly 1
        CHECK(row->entropyNorm == Approx(1.0).epsilon(1e-12));
        // rates (1/3, 1/7, 1/7) -> q = (7/13, 3/13, 3/13)
        const double expected = entropyOf({7.0 / 13.0, 3.0 / 13.0, 3.0 / 13.0}) /
                                std::log2(3.0);
        CHECK(row->entropyRateNorm == Approx(expected).epsilon(1e-12));
        CHECK(row->entropyRateNorm < 1.0);
    }

    SECTION("'silver': the longer document absorbs its extra hit") {
        const mse::TermRow* row = findRow(count, "silver");
        REQUIRE(row != nullptr);
        REQUIRE(row->tf == 3);  // once in doc1, TWICE in doc3
        REQUIRE(row->df == 2);
        // raw counts (1, 2) -> p = (1/3, 2/3): clearly non-uniform
        const double rawExpected = entropyOf({1.0 / 3.0, 2.0 / 3.0}) / std::log2(2.0);
        CHECK(row->entropyNorm == Approx(rawExpected).epsilon(1e-12));
        // rates (1/3, 2/8) -> q = (4/7, 3/7): doc3 being twice as long
        // absorbs its second occurrence, so the rate is nearly uniform --
        // exactly the length-bias correction entropyRateNorm exists for
        const double rateExpected = entropyOf({4.0 / 7.0, 3.0 / 7.0}) / std::log2(2.0);
        CHECK(row->entropyRateNorm == Approx(rateExpected).epsilon(1e-12));
        CHECK(row->entropyRateNorm > row->entropyNorm);
    }

    SECTION("single-document terms have all-zero normalized entropy") {
        const mse::TermRow* row = findRow(count, "damaged");  // doc2 only
        REQUIRE(row != nullptr);
        CHECK(row->entropy == 0.0);
        CHECK(row->entropyNorm == 0.0);
        CHECK(row->entropyRateNorm == 0.0);
    }
}

TEST_CASE("deriveStopWords applies the three-feature rule", "[count]") {
    mse::WordCount count;
    count.documents = 10;

    // Hand-built rows exercising every failure mode of the rule. Order is
    // the table's (tf desc, term asc); deriveStopWords must preserve it.
    auto row = [](const char* term, std::uint64_t tf, double dfRatio,
                  double tfShare, double rateNorm) {
        mse::TermRow r;
        r.term = term;
        r.tf = tf;
        r.df = static_cast<std::uint32_t>(dfRatio * 10);
        r.dfRatio = dfRatio;
        r.tfShare = tfShare;
        r.entropyRateNorm = rateNorm;
        return r;
    };
    count.rows.push_back(row("wideflat", 500, 0.9, 0.02, 0.97));   // passes all
    count.rows.push_back(row("peaked", 400, 0.9, 0.02, 0.90));     // not flat
    count.rows.push_back(row("narrow", 300, 0.50, 0.02, 0.99));    // not wide
    count.rows.push_back(row("thin", 200, 0.9, 0.0001, 0.99));     // not heavy
    count.rows.push_back(row("borderline", 100, 0.75, 0.001, 0.93));  // == all

    SECTION("default rule keeps only wide+heavy+flat terms") {
        const std::vector<std::string> words = mse::deriveStopWords(count, mse::StopWordRule{});
        REQUIRE(words.size() == 2);
        CHECK(words[0] == "wideflat");
        CHECK(words[1] == "borderline");  // >= semantics, boundaries included
    }

    SECTION("looser thresholds widen the net") {
        mse::StopWordRule loose;
        loose.minDfRatio = 0.4;
        loose.minTfShare = 0.00005;
        loose.minEntropyRateNorm = 0.85;
        const std::vector<std::string> words = mse::deriveStopWords(count, loose);
        REQUIRE(words.size() == 5);
    }

    SECTION("a stricter entropy cut removes everything") {
        mse::StopWordRule strict;
        strict.minEntropyRateNorm = 0.99;
        CHECK(mse::deriveStopWords(count, strict).empty());
    }
}
