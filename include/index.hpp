// index.hpp -- in-memory inverted index + v1 binary serialization.
//
// Build pipeline (plan.md 6.3): tokenize -> [stop words] -> [Porter stem] ->
// Dictionary::upsert -> PostingList::append; then the dictionary is frozen in
// lexicographic order (sortedEntries()) into the Index. The same Index type
// is produced by buildIndex() and by loadIndex(), so the writer and the
// reader share one specification (plan.md 7) and `mse dump` can verify the
// round trip byte for byte.

#pragma once

#include <cstddef>
#include <cstdint>
#include <iosfwd>
#include <string>
#include <vector>

#include "corpus.hpp"
#include "dict.hpp"
#include "posting.hpp"
#include "tokenizer.hpp"

namespace mse {

class StopWords;  // stopwords.hpp

constexpr std::uint32_t kIndexVersion = 1;
constexpr std::uint32_t kIndexFlagStopWords = 1u << 0;  // stop word filter applied
constexpr std::uint32_t kIndexFlagStemmed = 1u << 1;    // Porter stemmer applied

struct IndexDoc {
    std::uint32_t docID = 0;       // 1-based; docs[i].docID == i + 1
    std::string title;
    std::uint32_t keptTokens = 0;  // this document's tokens in the index
};

struct IndexTerm {
    std::string term;
    std::vector<Posting> postings;         // docID strictly increasing
    std::vector<std::uint32_t> positions;  // arena; slices via posStart/tf
};

struct Index {
    std::uint32_t flags = 0;       // kIndexFlag* bits, recorded in the file
    std::uint64_t tokenCount = 0;  // kept tokens across the corpus
    std::vector<IndexDoc> docs;
    std::vector<IndexTerm> terms;  // sorted by term
};

struct BuildOptions {
    TokenizerOptions tokenizer;
    const StopWords* stopWords = nullptr;  // optional filter, applied first
    bool stem = false;                     // Porter-stem the survivors
};

// Build the full index over `corpus` using `dictionary` (any Dictionary
// implementation; splay and hash must produce identical Index objects,
// plan.md 6.5). The dictionary is left intact -- entries are copied.
Index buildIndex(const Corpus& corpus, const BuildOptions& options,
                 Dictionary& dictionary);

// v1 binary format, little-endian (spec: plan.md 7). Throws on I/O errors.
void writeIndex(const std::string& path, const Index& index);

// Reads and validates an index file (structure invariants included).
Index loadIndex(const std::string& path);

// Structural invariants: dense docIDs, strictly increasing docIDs per
// posting list, tf == positions per posting, term table strictly sorted,
// sums consistent with tokenCount. Throws std::runtime_error on violation.
void validateIndex(const Index& index);

// Human-readable posting dump (positions capped per posting). Returns false
// when the term is not in the index.
bool dumpTerm(const Index& index, const std::string& term, std::size_t maxPostings,
              std::ostream& out);

}  // namespace mse
