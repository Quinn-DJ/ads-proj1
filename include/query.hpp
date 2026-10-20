// query.hpp -- query processing over a built index (project task 3).
//
// The query side re-runs *exactly* the pipeline the index was built with
// (flags recorded in the index header): tokenize -> stop word filter ->
// Porter stemmer. That is also where the four query thresholds live:
//
//   --max-df <ratio>   drop query terms whose df/documentCount exceeds the
//                      ratio (too common to discriminate; plan.md 9)
//   --min-df <n>       drop query terms appearing in fewer than n documents
//   --min-match <k>    keep documents that match at least k of the search
//                      terms (0 = all of them, i.e. AND semantics; 1 = OR)
//   --top-x <n>        keep only the n best documents (score order below)
//
// Score order: number of matched terms (desc), then summed tf (desc), then
// docID (asc) -- deterministic, which is what the Task 4 threshold matrix
// compares against.

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "index.hpp"

namespace mse {

class StopWords;

enum class QueryMode {
  Word,     // exactly one (normalized) term
  Phrase,   // the terms must occur adjacently in the filtered stream
  Boolean,  // multiple terms combined by --min-match
};

struct QueryOptions {
  QueryMode mode = QueryMode::Word;
  double maxDfRatio = 1.0;     // 1.0 = off
  std::uint32_t minDf = 1;     // 1 = off
  std::uint32_t minMatch = 0;  // 0 = require all searchable terms
  std::size_t topX = 0;        // 0 = unlimited
};

struct QueryHit {
  std::uint32_t docID = 0;
  std::uint32_t matched = 0;        // search terms that hit this document
  std::uint64_t tfSum = 0;          // sum of their tf in this document
  std::uint32_t firstPosition = 0;  // filtered-stream position of the match
};

// A query term that could not be used, with the reason shown to the user
// (stop word, not in the index, dropped by a threshold, ...).
struct TermNote {
  std::string term;
  std::string reason;
};

struct QueryResult {
  std::vector<QueryHit> hits;      // score order, capped by topX
  std::vector<std::string> terms;  // searchable terms actually used
  std::vector<TermNote> notes;     // dropped terms and why
};

// Run a word / phrase / boolean query. `stopWords` should be the same list
// the index was built with (it is only consulted when the index flags say
// stop words were applied). Thresholds follow plan.md section 9.
QueryResult runQuery(const Index& index, const std::string& queryText,
                     const QueryOptions& options, const StopWords* stopWords);

// For --pretty: the source line containing the filtered-stream position
// `position` of `doc` (the pipeline is re-run to map position -> line).
// Returns an empty string when the position cannot be located.
std::string lineOfPosition(const Document& doc, const Index& index,
                           const StopWords* stopWords, std::uint32_t position);

}  // namespace mse
