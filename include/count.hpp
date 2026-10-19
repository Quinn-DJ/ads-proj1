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
// Document-distribution entropy (the third stop-word feature, notes.md 3.6):
// with p_d = tf(t,d) / tf(t) over the documents d that contain the term,
//   entropy         = H(t) = -sum_d p_d * log2(p_d)      (bits)
//   entropyNorm     = H(t) / log2(df)                    (0..1, 1 = uniform)
// On a corpus whose documents differ wildly in length (a play has ~30k
// tokens, a sonnet ~100), a stop word's occurrences follow the *document
// length*, so raw H(t) ranks content words above stop words and entropyNorm
// alone does not discriminate. The fix is to normalize by length first:
// with rate_d = tf(t,d) / keptTokens(d) and q_d = rate_d / sum(rate),
//   entropyRateNorm = -sum_d q_d*log2(q_d) / log2(df)
// A term sprinkled at a constant *rate* (classic stop-word behaviour) has
// entropyRateNorm near 1; a topical word (rate concentrated where it is
// about) and a one-document name (df == 1, defined 0) sit far below.
// deriveStopWords() below combines width/mass/flatness into one rule.
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
    double entropy = 0.0;        // raw H(t) in bits (see file header)
    double entropyNorm = 0.0;    // H(t) / log2(df); 0 when df == 1
    double entropyRateNorm = 0.0;  // length-normalized variant (see header)
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

// Three-feature stop-word rule (the entropy-based extension of plan.md 4.2):
// a term is a *derived* stop word when it is
//   wide -- dfRatio         >= minDfRatio        (spread over most documents)
//   heavy-- tfShare         >= minTfShare        (carries real token mass)
//   flat -- entropyRateNorm >= minEntropyRateNorm (constant rate per document)
// The defaults reproduce the spirit of the hand-reviewed 39-word list; the
// thresholds exist so the sensitivity can be swept experimentally.
struct StopWordRule {
    double minDfRatio = 0.75;
    double minTfShare = 0.001;
    double minEntropyRateNorm = 0.93;
};

// Apply the rule to a counted table. Returns the matching terms in the
// table's order (tf descending, term ascending); deterministic and pure.
std::vector<std::string> deriveStopWords(const WordCount& count,
                                         const StopWordRule& rule);

// Write the table as CSV (header included, ratios as fixed 6 decimals).
// Creates parent directories if needed; throws std::runtime_error on failure.
void writeWordCountCsv(const std::string& path, const WordCount& result);

}  // namespace mse
