// count.cpp -- implementation of the word counter (see count.hpp).

#include "count.hpp"

#include "stemmer.hpp"
#include "stopwords.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <unordered_map>

namespace mse {

namespace {

// Running per-term statistics.
//
// `lastDoc` is the key to O(1) df updates. Documents are processed one at a
// time and docIDs are increasing, so if the previous token of this term came
// from the same document (lastDoc == current id) the term is already counted
// for df; otherwise this is its first occurrence in the current document and
// df is incremented. No per-document set of seen terms is needed.
struct TermStats {
    std::uint64_t tf = 0;      // total occurrences in the corpus
    std::uint32_t df = 0;      // number of documents containing the term
    std::uint32_t lastDoc = 0; // document that produced the latest occurrence
};

// Coarse candidate rule used for the `stop_candidate` CSV column (plan.md
// 4.2). This is deliberately stricter than the 0.40 band used for the final
// manual analysis: the flag is a quick report, not the stop word list itself.
constexpr double kCandidateDfRatio = 0.75;
constexpr double kCandidateTfShare = 0.001;

}  // namespace

// Count tf and df over the whole corpus.
//
// Pipeline per token: tokenize -> optional stop word filter -> optional Porter
// stemming -> table update. Stop word and stem flags are independent so the
// four report variants (raw / stop / stem / stem+stop) all use this function.
// Only tokens that survive the stop word filter contribute to `tokens`; the
// removed ones are tracked separately in `stopwordTokens`. This distinction
// matters because tf_share must use the kept-token total as its denominator.
//
// Complexity: O(total tokens) expected time with the hash table; O(#distinct
// terms) extra memory. Stemming is memoized per distinct surface form.
WordCount countWords(const Corpus& corpus, const CountOptions& options) {
    const Tokenizer tokenizer(options.tokenizer);
    std::unordered_map<std::string, TermStats> table;
    std::unordered_map<std::string, std::string> stemCache;
    std::vector<Token> tokens;

    // Stemming is deterministic, so each distinct word form only needs to be
    // stemmed once even though it may occur thousands of times. The cache maps
    // surface form -> stem; returning a reference avoids copying on every
    // occurrence and keeps the hot loop allocation-light.
    const auto stemmed = [&stemCache](const std::string& word) -> const std::string& {
        auto it = stemCache.find(word);
        if (it == stemCache.end()) {
            it = stemCache.emplace(word, stem(word)).first;
        }
        return it->second;
    };

    WordCount result;
    result.documents = corpus.size();

    for (std::size_t i = 0; i < corpus.size(); ++i) {
        const Document& doc = corpus[i];
        tokens.clear();
        tokenizer.tokenize(doc, tokens);
        for (const Token& token : tokens) {
            // Stop words are removed before stemming/counting. They are still
            // reported separately so the caller can print the before/after
            // comparison and compute the correct tf_share denominator.
            if (options.stopWords != nullptr &&
                options.stopWords->contains(token.text)) {
                ++result.stopwordTokens;
                continue;
            }
            ++result.tokens;  // count only tokens that survive the filter
            const std::string& term =
                options.stem ? stemmed(token.text) : token.text;
            TermStats& stats = table[term];
            ++stats.tf;
            // df counts documents, not occurrences: update it only on the
            // first hit of this term in the current document (see TermStats).
            if (stats.lastDoc != static_cast<std::uint32_t>(doc.id)) {
                ++stats.df;
                stats.lastDoc = static_cast<std::uint32_t>(doc.id);
            }
        }
    }

    // Materialize one row per term and derive the two ratios used by the
    // stop-word analysis. tf_share divides by `result.tokens` (kept tokens
    // only), not by the pre-filter total; using the wrong denominator was one
    // of the bugs caught by the Python cross-check (notes.md 4.2 #3).
    result.rows.reserve(table.size());
    for (auto& entry : table) {
        TermRow row;
        row.term = std::move(entry.first);
        row.tf = entry.second.tf;
        row.df = entry.second.df;
        row.dfRatio = static_cast<double>(row.df) / static_cast<double>(result.documents);
        row.tfShare = static_cast<double>(row.tf) / static_cast<double>(result.tokens);
        row.stopCandidate = row.dfRatio >= kCandidateDfRatio &&
                            row.tfShare >= kCandidateTfShare;
        result.rows.push_back(std::move(row));
    }

    // Sort by tf descending for the report, with the term as a deterministic
    // tie-break. Determinism matters: the CSV is compared byte-for-byte with
    // the independent Python implementation.
    std::sort(result.rows.begin(), result.rows.end(),
              [](const TermRow& a, const TermRow& b) {
                  if (a.tf != b.tf) {
                      return a.tf > b.tf;
                  }
                  return a.term < b.term;  // deterministic tie-break
              });

    result.distinctTerms = result.rows.size();
    for (const TermRow& row : result.rows) {
        if (row.tf == 1) {
            ++result.hapax;
        }
    }
    return result;
}

// Write the full table as CSV, sorted as produced by countWords().
//
// The header and row order are part of the cross-check contract with
// tools/check_wordcount.py (byte-identical output). Ratios use six fixed
// decimals; rank is 1-based. Parent directories are created on demand, and any
// I/O failure throws with the path. Complexity: O(rows) I/O.
void writeWordCountCsv(const std::string& path, const WordCount& result) {
    const std::filesystem::path target(path);
    if (target.has_parent_path()) {
        std::filesystem::create_directories(target.parent_path());
    }

    std::ofstream out(path);
    if (!out) {
        throw std::runtime_error("cannot write CSV file: " + path);
    }
    out << "rank,term,tf,df,df_ratio,tf_share,stop_candidate\n";
    out.setf(std::ios::fixed);
    out.precision(6);
    for (std::size_t i = 0; i < result.rows.size(); ++i) {
        const TermRow& row = result.rows[i];
        out << (i + 1) << ',' << row.term << ',' << row.tf << ',' << row.df << ','
            << row.dfRatio << ',' << row.tfShare << ','
            << (row.stopCandidate ? 1 : 0) << '\n';
    }
}

}  // namespace mse
