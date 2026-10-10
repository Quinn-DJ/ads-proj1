// count.cpp -- implementation of the word counter (see count.hpp).

#include "count.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <unordered_map>

namespace mse {

namespace {

// Running per-term statistics. lastDoc remembers the document that produced
// the most recent token, so df is incremented once per document even when a
// term occurs many times inside it (documents are processed one at a time).
struct TermStats {
    std::uint64_t tf = 0;
    std::uint32_t df = 0;
    std::uint32_t lastDoc = 0;
};

// plan.md 4.2: candidate rule for the stop-word analysis.
constexpr double kCandidateDfRatio = 0.75;
constexpr double kCandidateTfShare = 0.001;

}  // namespace

WordCount countWords(const Corpus& corpus, const TokenizerOptions& options) {
    const Tokenizer tokenizer(options);
    std::unordered_map<std::string, TermStats> table;
    std::vector<Token> tokens;

    WordCount result;
    result.documents = corpus.size();

    for (std::size_t i = 0; i < corpus.size(); ++i) {
        const Document& doc = corpus[i];
        tokens.clear();
        tokenizer.tokenize(doc, tokens);
        result.tokens += tokens.size();
        for (const Token& token : tokens) {
            TermStats& stats = table[token.text];
            ++stats.tf;
            if (stats.lastDoc != static_cast<std::uint32_t>(doc.id)) {
                ++stats.df;
                stats.lastDoc = static_cast<std::uint32_t>(doc.id);
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
