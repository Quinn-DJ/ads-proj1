// count.cpp -- implementation of the word counter (see count.hpp).

#include "count.hpp"

#include "stemmer.hpp"
#include "stopwords.hpp"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <unordered_map>

namespace mse {

namespace {

// Running per-term statistics. lastDoc remembers the document that produced
// the most recent token, so df is incremented once per document even when a
// term occurs many times inside it (documents are processed one at a time).
// docIds/docTf keep one (document, count) pair per supporting document,
// appended in docID order -- exactly the distribution the entropy features
// need, and the same order the Python checker sums in.
struct TermStats {
    std::uint64_t tf = 0;
    std::uint32_t df = 0;
    std::uint32_t lastDoc = 0;
    std::vector<std::uint32_t> docIds;  // docID of each support entry
    std::vector<std::uint32_t> docTf;   // tf within that document
};

// plan.md 4.2: candidate rule for the stop-word analysis.
constexpr double kCandidateDfRatio = 0.75;
constexpr double kCandidateTfShare = 0.001;

}  // namespace

WordCount countWords(const Corpus& corpus, const CountOptions& options) {
    const Tokenizer tokenizer(options.tokenizer);
    std::unordered_map<std::string, TermStats> table;
    std::unordered_map<std::string, std::string> stemCache;
    std::vector<Token> tokens;

    // Stemming is deterministic, so each distinct word form only needs to be
    // stemmed once even though it may occur thousands of times.
    const auto stemmed = [&stemCache](const std::string& word) -> const std::string& {
        auto it = stemCache.find(word);
        if (it == stemCache.end()) {
            it = stemCache.emplace(word, stem(word)).first;
        }
        return it->second;
    };

    WordCount result;
    result.documents = corpus.size();

    // Kept tokens per document (after the stop-word filter): the denominator
    // of the per-document "rate" used by entropyRateNorm.
    std::vector<std::uint64_t> docTokens(corpus.size(), 0);

    for (std::size_t i = 0; i < corpus.size(); ++i) {
        const Document& doc = corpus[i];
        tokens.clear();
        tokenizer.tokenize(doc, tokens);
        for (const Token& token : tokens) {
            if (options.stopWords != nullptr &&
                options.stopWords->contains(token.text)) {
                ++result.stopwordTokens;
                continue;
            }
            ++result.tokens;  // count only tokens that survive the filter
            ++docTokens[i];
            const std::string& term =
                options.stem ? stemmed(token.text) : token.text;
            TermStats& stats = table[term];
            ++stats.tf;
            if (stats.lastDoc != static_cast<std::uint32_t>(doc.id)) {
                ++stats.df;
                stats.lastDoc = static_cast<std::uint32_t>(doc.id);
                stats.docIds.push_back(static_cast<std::uint32_t>(doc.id));
                stats.docTf.push_back(1);  // first hit in this document
            } else {
                ++stats.docTf.back();  // another hit in the same document
            }
        }
    }

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
        // Entropy over the term's document distribution (count.hpp header).
        // Raw: H = -sum p*log2(p) with p = docTf[i]/tf. Rate-normalized:
        // q = (docTf[i]/docTokens[doc]) / sum(same), then H(q)/log2(df).
        // docIds/docTf are visited in insertion order, matching the Python
        // checker, so both sides arrive at bit-identical doubles (the
        // cross-check diffs the CSV byte for byte).
        double entropy = 0.0;
        double rateSum = 0.0;
        for (std::size_t d = 0; d < entry.second.docTf.size(); ++d) {
            const double p =
                static_cast<double>(entry.second.docTf[d]) / static_cast<double>(row.tf);
            entropy -= p * std::log2(p);
            rateSum += static_cast<double>(entry.second.docTf[d]) /
                       static_cast<double>(docTokens[entry.second.docIds[d] - 1]);
        }
        row.entropy = entropy;
        row.entropyNorm =
            row.df >= 2 ? entropy / std::log2(static_cast<double>(row.df)) : 0.0;
        double rateEntropy = 0.0;
        for (std::size_t d = 0; d < entry.second.docTf.size(); ++d) {
            const double q =
                (static_cast<double>(entry.second.docTf[d]) /
                 static_cast<double>(docTokens[entry.second.docIds[d] - 1])) /
                rateSum;
            rateEntropy -= q * std::log2(q);
        }
        row.entropyRateNorm = row.df >= 2
                                  ? rateEntropy / std::log2(static_cast<double>(row.df))
                                  : 0.0;
        result.rows.push_back(std::move(row));
    }

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

std::vector<std::string> deriveStopWords(const WordCount& count,
                                         const StopWordRule& rule) {
    std::vector<std::string> words;
    for (const TermRow& row : count.rows) {
        if (row.dfRatio >= rule.minDfRatio && row.tfShare >= rule.minTfShare &&
            row.entropyRateNorm >= rule.minEntropyRateNorm) {
            words.push_back(row.term);
        }
    }
    return words;  // already in table order (tf desc, term asc)
}

void writeWordCountCsv(const std::string& path, const WordCount& result) {
    const std::filesystem::path target(path);
    if (target.has_parent_path()) {
        std::filesystem::create_directories(target.parent_path());
    }

    std::ofstream out(path);
    if (!out) {
        throw std::runtime_error("cannot write CSV file: " + path);
    }
    out << "rank,term,tf,df,df_ratio,tf_share,stop_candidate,"
           "entropy,entropy_norm,entropy_rate_norm\n";
    out.setf(std::ios::fixed);
    out.precision(6);
    for (std::size_t i = 0; i < result.rows.size(); ++i) {
        const TermRow& row = result.rows[i];
        out << (i + 1) << ',' << row.term << ',' << row.tf << ',' << row.df << ','
            << row.dfRatio << ',' << row.tfShare << ','
            << (row.stopCandidate ? 1 : 0) << ','
            << row.entropy << ',' << row.entropyNorm << ','
            << row.entropyRateNorm << '\n';
    }
}

}  // namespace mse
