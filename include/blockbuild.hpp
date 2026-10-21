// SPIMI external construction: spill blocks, merge with bounded fan-in, and
// stream the final postings and term table through temporary files.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

#include "index.hpp"

namespace mse {

struct BlockBuildOptions {
  BuildOptions build;
  std::size_t blockTerms = 4096;
  // Estimated dictionary/cache allocations, including posting/position capacity.
  // Spills occur between documents, so budgets may overshoot by one document.
  std::size_t blockBytes = 8 * 1024 * 1024;
  std::size_t mergeFanIn = 32;  // maximum simultaneously open input runs
  bool compress = false;
  std::string blockDir = "build/blockbuild";
  std::string blockPrefix = "run";
};

struct BlockBuildStats {
  std::size_t documents = 0;
  std::size_t terms = 0;
  std::uint64_t tokenCount = 0;
  std::size_t blocks = 0;
  std::size_t maxLiveTerms = 0;
  std::size_t maxLiveBytes = 0;  // estimated block/cache bytes, not process RSS
  std::size_t maxMergeCursors = 0;
  std::size_t mergePasses = 0;  // includes the final merge to index postings
  double scanSeconds = 0.0;
  double mergeSeconds = 0.0;
  std::uint64_t blockBytes = 0;  // initial and intermediate run bytes written
};

// Write v1/v2 bytes identical to the ordinary builder, without a complete
// Index or posting list in memory. Memory includes document metadata, run
// paths, a scan block (plus one document), and mergeFanIn buffered readers.
// Temporary files are isolated per invocation and removed on success. On
// error the exception identifies the workspace retained for diagnosis.
void buildIndexBlocked(const std::string& manifestPath,
                       const std::string& indexPath,
                       const BlockBuildOptions& options,
                       BlockBuildStats& stats);

}  // namespace mse
