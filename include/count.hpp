// count.hpp -- word counting over the corpus (project task 1, 1 pt).
//
// Walks every document, tokenizes it (tokenizer.hpp) and accumulates, per
// normalized term:
//   tf  -- total occurrences in the whole corpus
//   df  -- number of documents that contain the term at least once
// plus the derived ratios used by the stop-word analysis (plan.md 4.2):
//   df_ratio = df / documents,  tf_share = tf / tokens
// A term is flagged as a *candidate* stop word when
//   df_ratio >= 0.75  AND  tf_share >= 0.1%
// The flag is deliberately coarse: the final stop-word list comes out of the
// manual review step of the candidate zone (see plan.md 4.2).
//
// The token pipeline applied by countWords():
//   token -> [stop word filter] -> [Porter stemmer] -> count
// Both stages are optional; the number of tokens dropped by the stop word
// filter is reported in WordCount so the before/after comparison for task 1
// can be printed directly.

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "corpus.hpp"
#include "tokenizer.hpp"

namespace mse {

class StopWords;

// One row of the wordcount table, sorted by tf descending then term ascending.
struct TermRow {
    std::string term;
    std::uint64_t tf = 0;        // total occurrences
    std::uint32_t df = 0;        // documents containing the term
    double dfRatio = 0.0;        // df / documents
    double tfShare = 0.0;        // tf / tokens
    bool stopCandidate = false;  // dfRatio >= 0.75 && tfShare >= 0.001
};

struct WordCount {
    std::vector<TermRow> rows;   // complete table, sorted
    std::uint64_t tokens = 0;    // tokens over all documents (after filtering)
    std::uint64_t stopwordTokens = 0;  // tokens dropped by the stop word filter
    std::size_t documents = 0;
    std::size_t distinctTerms = 0;
    std::size_t hapax = 0;       // terms with tf == 1
};

struct CountOptions {
    TokenizerOptions tokenizer;            // which line kinds contribute tokens
    const StopWords* stopWords = nullptr;  // drop these tokens first (optional)
    bool stem = false;                     // Porter-stem the survivors (optional)
};

// Count tf/df over the whole corpus.
// Tokens in `options.stopWords` are skipped (counted in stopwordTokens);
// when `options.stem` is set the surviving tokens are stemmed with the
// Porter algorithm before counting, so index and query stay consistent.
WordCount countWords(const Corpus& corpus, const CountOptions& options);

// Write the table as CSV (header included, ratios as fixed 6 decimals).
// Creates parent directories if needed; throws std::runtime_error on failure.
void writeWordCountCsv(const std::string& path, const WordCount& result);

}  // namespace mse
