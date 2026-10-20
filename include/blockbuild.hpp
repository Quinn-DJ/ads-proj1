// blockbuild.hpp -- block-wise (SPIMI-style) external index construction.
//
// The bonus question's other half: 500,000 FILES do not stress the query
// side but the BUILD side -- the in-memory dictionary of a single-pass
// build cannot fit when the vocabulary is huge. This module demonstrates
// the standard answer on our corpus (plan.md 11):
//
//   phase 1 (blocks): stream the corpus document by document and
//     accumulate postings in a small in-memory dictionary; whenever it
//     holds `blockTerms` distinct terms, freeze it: sort the terms and
//     append the block to a temporary run file, then start a fresh
//     dictionary. Memory stays O(blockTerms) no matter the corpus.
//   phase 2 (merge): read every run as a sorted term stream and k-way
//     merge them by term into the final Index. Because blocks partition
//     the *document sequence*, a term's postings in later blocks always
//     carry larger docIDs -- merging is plain concatenation per term.
//
// The result is defined to be exactly what buildIndex() produces: the
// same flags, docs, sorted term table, postings and position arena. The
// `mse build-blocks` command prints block count / phase timings, and the
// tests assert byte-identical index files against the in-memory build.

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "corpus.hpp"
#include "index.hpp"

namespace mse {

struct BlockBuildOptions {
  BuildOptions build;             // shared pipeline options (tokenizer/filter)
  std::size_t blockTerms = 4096;  // in-memory vocabulary budget per block
  std::string blockDir = "build/blockbuild";  // where run files are written
  std::string blockPrefix = "run";            // run file names: run0000.bin
};

struct BlockBuildStats {
  std::size_t blocks = 0;        // run files written
  std::size_t maxLiveTerms = 0;  // high-water mark of the block dictionary
  double scanSeconds = 0.0;      // phase 1 (tokenize + block writes)
  double mergeSeconds = 0.0;     // phase 2 (k-way merge)
  std::uint64_t blockBytes = 0;  // total run-file bytes (disk traffic)
};

// Build `index` with the block-wise pipeline; fills `stats`. Throws on I/O
// errors or option problems (blockTerms must be >= 1). Block files are
// removed again on success (they are an implementation detail); on failure
// leftovers may remain and are reported in the error message's path.
Index buildIndexBlocked(const Corpus& corpus, const BlockBuildOptions& options,
                        BlockBuildStats& stats);

}  // namespace mse
