#include "blockbuild.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <limits>
#include <memory>
#include <queue>
#include <stdexcept>
#include <unordered_map>
#include <utility>
#include <vector>

#include "stemmer.hpp"
#include "stopwords.hpp"

namespace mse {
namespace {

void putU32(std::ostream& out, std::uint32_t value) {
  const char bytes[4] = {
      static_cast<char>(value), static_cast<char>(value >> 8),
      static_cast<char>(value >> 16), static_cast<char>(value >> 24)};
  out.write(bytes, sizeof(bytes));
}

void putU64(std::ostream& out, std::uint64_t value) {
  putU32(out, static_cast<std::uint32_t>(value));
  putU32(out, static_cast<std::uint32_t>(value >> 32));
}

std::uint32_t getU32(std::istream& in) {
  unsigned char bytes[4];
  if (!in.read(reinterpret_cast<char*>(bytes), sizeof(bytes))) {
    throw std::runtime_error("truncated block build temporary file");
  }
  return std::uint32_t(bytes[0]) | (std::uint32_t(bytes[1]) << 8) |
         (std::uint32_t(bytes[2]) << 16) | (std::uint32_t(bytes[3]) << 24);
}

std::uint64_t getU64(std::istream& in) {
  const std::uint64_t low = getU32(in);
  return low | (std::uint64_t(getU32(in)) << 32);
}

std::uint32_t asU32(std::uint64_t value) {
  if (value > std::numeric_limits<std::uint32_t>::max()) {
    throw std::runtime_error("block build exceeds index format's u32 limit");
  }
  return static_cast<std::uint32_t>(value);
}

void putString(std::ostream& out, const std::string& text) {
  putU32(out, asU32(text.size()));
  out.write(text.data(), static_cast<std::streamsize>(text.size()));
}

std::string getString(std::istream& in) {
  std::string text(getU32(in), '\0');
  if (!in.read(text.data(), static_cast<std::streamsize>(text.size()))) {
    throw std::runtime_error("truncated block build string");
  }
  return text;
}

void finish(std::ofstream& out, const std::string& path) {
  out.flush();
  if (!out) throw std::runtime_error("cannot write block build file: " + path);
  out.close();
  if (!out) throw std::runtime_error("cannot close block build file: " + path);
}

// Only files inside this newly created directory are ever cleaned up.
class Workspace {
 public:
  explicit Workspace(const std::string& parent) {
    std::filesystem::create_directories(parent);
    const auto stamp =
        std::chrono::steady_clock::now().time_since_epoch().count();
    for (std::size_t attempt = 0;; ++attempt) {
      path = std::filesystem::path(parent) /
             ("mse-" + std::to_string(stamp) + "-" + std::to_string(attempt));
      if (std::filesystem::create_directory(path)) break;
    }
  }

  std::string file(const std::string& name) const {
    return (path / name).string();
  }
  void clean() const {
    std::error_code ignored;
    std::filesystem::remove_all(path, ignored);
  }
  std::filesystem::path path;
};

// Run format: u32 term count, then {string term, u32 df,
// repeated df times {u32 docID, u32 tf, tf * u32 positions}}.
// Only the term header is buffered: even a corpus-wide frequent term is read
// position by position, rather than loaded once for each run.
class RunCursor {
 public:
  explicit RunCursor(const std::string& path) : file(path, std::ios::binary) {
    if (!file) throw std::runtime_error("cannot reopen block run: " + path);
    remaining_ = getU32(file);
    advance();
  }

  void advance() {
    active = remaining_ != 0;
    if (!active) return;
    --remaining_;
    const std::string previous = std::move(term);
    term = getString(file);
    postings = getU32(file);
    if (term.empty() || (!previous.empty() && term <= previous) ||
        postings == 0) {
      throw std::runtime_error("invalid block run term header");
    }
  }

  std::ifstream file;
  std::string term;
  std::uint32_t postings = 0;
  bool active = false;

 private:
  std::uint32_t remaining_ = 0;
};

using Live = std::unordered_map<std::string, PostingList>;

void writeRun(const std::string& path, const Live& live) {
  // Borrow keys/lists instead of copying the dictionary's strings.
  std::vector<const Live::value_type*> sorted;
  sorted.reserve(live.size());
  for (const auto& entry : live) sorted.push_back(&entry);
  std::sort(sorted.begin(), sorted.end(),
            [](const auto* a, const auto* b) { return a->first < b->first; });
  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  putU32(out, asU32(sorted.size()));
  for (const auto* entry : sorted) {
    putString(out, entry->first);
    const PostingList& list = entry->second;
    putU32(out, asU32(list.df()));
    for (const Posting& posting : list.postings()) {
      putU32(out, posting.docID);
      putU32(out, posting.tf);
      for (std::uint32_t k = 0; k < posting.tf; ++k) {
        putU32(out, list.positions()[posting.posStart + k]);
      }
    }
  }
  finish(out, path);
}

struct MergeSummary {
  std::uint32_t terms = 0;
  std::uint64_t tokens = 0;
};

// With a term-table stream, emit final postings and metadata with relative
// offsets. Otherwise emit another sorted run, using seek-back count fields.
MergeSummary mergeRuns(const std::vector<std::string>& paths, std::ostream& out,
                       std::ostream* table, bool compressed,
                       BlockBuildStats& stats) {
  std::vector<std::unique_ptr<RunCursor>> cursors;
  for (const auto& path : paths)
    cursors.push_back(std::make_unique<RunCursor>(path));
  stats.maxMergeCursors = std::max(stats.maxMergeCursors, cursors.size());
  const auto later = [&cursors](std::size_t a, std::size_t b) {
    if (cursors[a]->term != cursors[b]->term)
      return cursors[a]->term > cursors[b]->term;
    return a > b;  // run order is document order, including intermediate runs
  };
  std::priority_queue<std::size_t, std::vector<std::size_t>, decltype(later)>
      heap(later);
  for (std::size_t i = 0; i < cursors.size(); ++i) {
    if (cursors[i]->active) heap.push(i);
  }
  if (table == nullptr) putU32(out, 0);  // patched once all terms are known
  MergeSummary summary;
  while (!heap.empty()) {
    const std::string term = cursors[heap.top()]->term;
    std::streampos countAt{};
    if (table == nullptr) {
      putString(out, term);
      countAt = out.tellp();
      putU32(out, 0);
    }
    const auto start = out.tellp();
    std::uint64_t df = 0, tf = 0;
    std::uint32_t previousDoc = 0;
    do {
      const std::size_t at = heap.top();
      heap.pop();  // remove before advance() mutates the heap's comparison key
      RunCursor& cursor = *cursors[at];
      for (std::uint32_t i = 0; i < cursor.postings; ++i) {
        const std::uint32_t doc = getU32(cursor.file);
        const std::uint32_t count = getU32(cursor.file);
        if (doc <= previousDoc || count == 0)
          throw std::runtime_error("invalid block run posting order/count");
        if (compressed) {
          out << encodeVarint(doc - previousDoc) << encodeVarint(count);
        } else {
          putU32(out, doc);
          putU32(out, count);
        }
        previousDoc = doc;
        std::uint32_t previousPos = 0;
        for (std::uint32_t k = 0; k < count; ++k) {
          const std::uint32_t position = getU32(cursor.file);
          if (k != 0 && position <= previousPos)
            throw std::runtime_error("invalid block run position order");
          if (compressed)
            out << encodeVarint(position - previousPos);
          else
            putU32(out, position);
          previousPos = position;
        }
        ++df;
        tf += count;
      }
      cursor.advance();
      if (cursor.active) heap.push(at);
    } while (!heap.empty() && cursors[heap.top()]->term == term);

    const auto end = out.tellp();
    if (!out) throw std::runtime_error("cannot write merged postings");
    if (table == nullptr) {
      out.seekp(countAt);
      putU32(out, asU32(df));
      out.seekp(end);
    } else {
      const std::uint32_t length =
          asU32(static_cast<std::uint64_t>(end - start));
      if (compressed && length >= (1u << kDictLenBits))
        throw std::runtime_error("term '" + term +
                                 "' has a compressed block over 2 MiB");
      putString(*table, term);
      putU32(*table, asU32(df));
      putU64(*table, tf);
      putU64(*table,
             static_cast<std::uint64_t>(static_cast<std::streamoff>(start)));
      putU32(*table, length);
    }
    summary.terms = asU32(std::uint64_t(summary.terms) + 1);
    summary.tokens += tf;
  }
  if (table == nullptr) {
    const auto end = out.tellp();
    out.seekp(0);
    putU32(out, summary.terms);
    out.seekp(end);
  }
  return summary;
}

// Copy in a fixed buffer; neither file is ever slurped back into memory.
void copyFile(const std::string& path, std::ostream& out) {
  std::ifstream in(path, std::ios::binary);
  if (!in) throw std::runtime_error("cannot reopen temporary file: " + path);
  std::array<char, 64 * 1024> buffer;
  while (in.read(buffer.data(), buffer.size()) || in.gcount() != 0) {
    out.write(buffer.data(), in.gcount());
  }
  if (in.bad()) throw std::runtime_error("cannot read temporary file: " + path);
}

void assembleIndex(const std::string& path, const std::vector<IndexDoc>& docs,
                   std::uint32_t flags, const BlockBuildStats& stats,
                   bool compressed, const std::string& tablePath,
                   const std::string& postingsPath) {
  std::uint64_t postingsBase = 28 + std::filesystem::file_size(tablePath);
  for (const auto& doc : docs) postingsBase += 12 + doc.title.size();
  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  out.write("MSE1", 4);
  putU32(out, compressed ? kIndexVersionCompressed : kIndexVersion);
  putU32(out, flags);
  putU32(out, asU32(docs.size()));
  putU32(out, asU32(stats.terms));
  putU64(out, stats.tokenCount);
  for (const auto& doc : docs) {
    putU32(out, doc.docID);
    putU32(out, doc.keptTokens);
    putString(out, doc.title);
  }
  std::ifstream table(tablePath, std::ios::binary);
  for (std::size_t i = 0; i < stats.terms; ++i) {
    putString(out, getString(table));
    putU32(out, getU32(table));
    putU64(out, getU64(table));
    putU64(out, postingsBase + getU64(table));
    putU32(out, getU32(table));
  }
  copyFile(postingsPath, out);
  finish(out, path);
}

}  // namespace

void buildIndexBlocked(const std::string& manifestPath,
                       const std::string& indexPath,
                       const BlockBuildOptions& options,
                       BlockBuildStats& stats) {
  if (options.blockTerms == 0 || options.blockBytes == 0 ||
      options.mergeFanIn < 2)
    throw std::runtime_error(
        "blockTerms/blockBytes must be positive; mergeFanIn must be >= 2");
  if (options.blockPrefix.empty() ||
      options.blockPrefix.find_first_of("/\\:") != std::string::npos)
    throw std::runtime_error("blockPrefix must be a plain filename prefix");
  stats = {};
  Workspace workspace(options.blockDir);
  try {
    const auto started = std::chrono::steady_clock::now();
    std::vector<IndexDoc> docs;
    std::vector<std::string> runs;
    // All scan allocations die before opening any merge cursors.
    {
      const Corpus corpus = Corpus::loadManifest(manifestPath);
      const Tokenizer tokenizer(options.build.tokenizer);
      std::vector<Token> tokens;
      Live live;
      std::size_t liveBytes = 0;
      std::unordered_map<std::string, std::string> stemCache;
      std::size_t cacheBytes = 0;
      const auto spill = [&]() {
        if (live.empty()) return;
        const std::string path = workspace.file(
            options.blockPrefix + std::to_string(runs.size()) + ".bin");
        writeRun(path, live);
        stats.blockBytes += std::filesystem::file_size(path);
        runs.push_back(path);
        live.clear();
        stemCache.clear();
        liveBytes = 0;
        cacheBytes = 0;
      };
      docs.reserve(corpus.size());
      for (std::size_t i = 0; i < corpus.size(); ++i) {
        const Document doc = corpus.readDocument(i);
        tokens.clear();
        tokenizer.tokenize(doc, tokens);
        std::uint32_t position = 0;
        for (const Token& token : tokens) {
          if (options.build.stopWords &&
              options.build.stopWords->contains(token.text))
            continue;
          const std::string* term = &token.text;
          if (options.build.stem) {
            auto cached = stemCache.find(token.text);
            if (cached == stemCache.end()) {
              // Many surface forms may stem to one term, so spills alone
              // cannot bound this cache. Cap it independently as well.
              if (stemCache.size() >= options.blockTerms ||
                  cacheBytes >= options.blockBytes) {
                stemCache.clear();
                cacheBytes = 0;
              }
              cached = stemCache.emplace(token.text, stem(token.text)).first;
              cacheBytes += sizeof(decltype(stemCache)::value_type) +
                            cached->first.capacity() +
                            cached->second.capacity() + 2 * sizeof(void*);
            }
            term = &cached->second;
          }
          auto entry = live.try_emplace(*term);
          if (entry.second) {
            liveBytes += sizeof(Live::value_type) +
                         entry.first->first.capacity() + 2 * sizeof(void*);
          }
          PostingList& list = entry.first->second;
          const auto oldPostings = list.postings().capacity();
          const auto oldPositions = list.positions().capacity();
          list.append(asU32(doc.id), position);
          liveBytes +=
              (list.postings().capacity() - oldPostings) * sizeof(Posting) +
              (list.positions().capacity() - oldPositions) *
                  sizeof(std::uint32_t);
          position = asU32(std::uint64_t(position) + 1);
        }
        docs.push_back(IndexDoc{asU32(doc.id), doc.title, position});
        stats.tokenCount += position;
        stats.maxLiveTerms = std::max(stats.maxLiveTerms, live.size());
        stats.maxLiveBytes =
            std::max(stats.maxLiveBytes, liveBytes + cacheBytes);
        if (live.size() >= options.blockTerms ||
            liveBytes + cacheBytes >= options.blockBytes)
          spill();
      }
      spill();
    }
    stats.documents = docs.size();
    stats.blocks = runs.size();
    stats.scanSeconds = std::chrono::duration<double>(
                            std::chrono::steady_clock::now() - started)
                            .count();
    const auto mergeStarted = std::chrono::steady_clock::now();
    std::size_t pass = 0;
    while (runs.size() > options.mergeFanIn) {
      std::vector<std::string> next;
      for (std::size_t begin = 0; begin < runs.size();
           begin += options.mergeFanIn) {
        const std::size_t end =
            std::min(runs.size(), begin + options.mergeFanIn);
        if (end - begin == 1) {
          next.push_back(runs[begin]);
          continue;
        }
        const std::vector<std::string> group(runs.begin() + begin,
                                             runs.begin() + end);
        const std::string path =
            workspace.file("merge-" + std::to_string(pass) + "-" +
                           std::to_string(next.size()) + ".run");
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        mergeRuns(group, out, nullptr, false, stats);
        finish(out, path);
        stats.blockBytes += std::filesystem::file_size(path);
        next.push_back(path);
        for (const auto& input : group) std::filesystem::remove(input);
      }
      runs = std::move(next);
      ++pass;
    }
    const std::string tablePath = workspace.file("terms.tmp");
    const std::string postingsPath = workspace.file("postings.tmp");
    std::ofstream table(tablePath, std::ios::binary | std::ios::trunc);
    std::ofstream postings(postingsPath, std::ios::binary | std::ios::trunc);
    const auto summary =
        mergeRuns(runs, postings, &table, options.compress, stats);
    finish(table, tablePath);
    finish(postings, postingsPath);
    if (summary.tokens != stats.tokenCount)
      throw std::runtime_error("block merge token count mismatch");
    stats.terms = summary.terms;
    stats.mergePasses = pass + 1;
    const std::uint32_t flags =
        (options.build.stopWords ? kIndexFlagStopWords : 0u) |
        (options.build.stem ? kIndexFlagStemmed : 0u);
    assembleIndex(indexPath, docs, flags, stats, options.compress, tablePath,
                  postingsPath);
    stats.mergeSeconds = std::chrono::duration<double>(
                             std::chrono::steady_clock::now() - mergeStarted)
                             .count();
    workspace.clean();
  } catch (const std::exception& error) {
    throw std::runtime_error("block build in " + workspace.path.string() +
                             ": " + error.what());
  }
}

}  // namespace mse
