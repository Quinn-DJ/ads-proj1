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

#include "bptree.hpp"

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

// Binary search over the sorted term table; nullptr when the term is absent.
// This is the query-side dictionary lookup (O(log #terms)).
const IndexTerm* findIndexTerm(const Index& index, const std::string& term);

// Human-readable posting dump (positions capped per posting). Returns false
// when the term is not in the index.
bool dumpTerm(const Index& index, const std::string& term, std::size_t maxPostings,
              std::ostream& out);

// ---------------------------------------------------------------------------
// Disk dictionary sidecar (the B-series scaling path, plan.md 11).
//
// writeIndex() already stores every term's (posting offset, byte length)
// in its term table. writeDictFile() additionally freezes that mapping
// into a B+Tree page file (bptree.hpp): term -> (offset << 21 | length),
// so a query can resolve any term with O(height) page reads and seek its
// postings directly -- without loading the term table (or the file).
//
// LazyIndexReader is the query side built on it: the small doc table is
// parsed eagerly (titles are needed for every hit); terms are resolved on
// demand through the B+Tree and only their posting bytes are read.
// ---------------------------------------------------------------------------

// Number of low bits of the packed value reserved for the posting length;
// offsets therefore reach 2^43 bytes and one term's postings 2 MiB.
constexpr std::uint64_t kDictLenBits = 21;

// Write the term->posting-location dictionary for `index` as a B+Tree page
// file. The offsets are computed exactly like writeIndex()'s, so both
// files describe the same index.bin. Throws on I/O errors.
void writeDictFile(const Index& index, const std::string& path,
                   std::uint32_t pageSize = 4096);

// Dictionary-driven reader over an existing index file + its sidecar.
class LazyIndexReader {
public:
    // Reads the header and the doc table of the index file; the term table
    // and the postings are never loaded as a whole.
    LazyIndexReader(const std::string& indexPath, const std::string& dictPath,
                    std::size_t cachePages = 8);

    std::uint32_t flags() const { return flags_; }
    std::uint64_t tokenCount() const { return tokenCount_; }
    const std::vector<IndexDoc>& docs() const { return docs_; }
    std::size_t termCount() const { return dict_.keyCount(); }

    // Parse one term's postings from its byte range. Returns false when
    // the term is absent. The parsed IndexTerm carries df (postings.size)
    // and the position arena exactly like the fully loaded form, so query
    // code can treat both interchangeably.
    bool readTerm(const std::string& term, IndexTerm& out) const;

    // Dictionary I/O counters (B+Tree page reads / cache hits) plus the
    // raw bytes read from the index file itself (header + doc table at
    // open, posting blocks per readTerm) -- the experiment's I/O measure.
    std::uint64_t dictPageReads() const { return dict_.pageReads(); }
    std::uint64_t dictCacheHits() const { return dict_.cacheHits(); }
    std::uint32_t dictHeight() const { return dict_.height(); }
    std::uint64_t bytesRead() const { return bytesRead_; }

    // Every (term -> packed location) pair in dictionary order: the
    // verification sweep walks this and re-parses each posting block.
    std::vector<std::pair<std::string, std::uint64_t>> allTerms() const;

private:
    std::string indexPath_;
    std::uint32_t flags_ = 0;
    std::uint64_t tokenCount_ = 0;
    std::vector<IndexDoc> docs_;
    BTreeFile dict_;
    mutable std::ifstream file_;  // index.bin, seeked per term (readTerm)
    mutable std::uint64_t bytesRead_ = 0; // bytes read from index.bin so far
};

}  // namespace mse
