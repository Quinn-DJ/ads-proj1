// blockbuild.cpp -- implementation of the SPIMI-style block build (see
// blockbuild.hpp). Two phases: document-streamed block runs with a bounded
// in-memory dictionary, then a k-way merge by term into the final Index.

#include "blockbuild.hpp"

#include "posting.hpp"
#include "stemmer.hpp"
#include "stopwords.hpp"
#include "tokenizer.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <memory>
#include <stdexcept>
#include <unordered_map>

namespace mse {

namespace {

// ---------------------------------------------------------------------------
// little-endian helpers (block run files use the same conventions as the
// index format; kept local so this module stays self-contained)
// ---------------------------------------------------------------------------

void putU32(std::ostream& out, std::uint32_t value) {
    const char bytes[4] = {static_cast<char>(value & 0xFFu),
                           static_cast<char>((value >> 8) & 0xFFu),
                           static_cast<char>((value >> 16) & 0xFFu),
                           static_cast<char>((value >> 24) & 0xFFu)};
    out.write(bytes, sizeof(bytes));
}

std::uint32_t getU32(std::istream& in) {
    char bytes[4];
    in.read(bytes, sizeof(bytes));
    if (!in || in.gcount() != static_cast<std::streamsize>(sizeof(bytes))) {
        throw std::runtime_error("block run file is truncated (u32 at " +
                                 std::to_string(static_cast<long long>(in.tellg())) +
                                 ")");
    }
    return static_cast<std::uint32_t>(static_cast<unsigned char>(bytes[0])) |
           (static_cast<std::uint32_t>(static_cast<unsigned char>(bytes[1])) << 8) |
           (static_cast<std::uint32_t>(static_cast<unsigned char>(bytes[2])) << 16) |
           (static_cast<std::uint32_t>(static_cast<unsigned char>(bytes[3])) << 24);
}

void putString(std::ostream& out, const std::string& text) {
    putU32(out, static_cast<std::uint32_t>(text.size()));
    out.write(text.data(), static_cast<std::streamsize>(text.size()));
}

std::string getString(std::istream& in) {
    const std::uint32_t length = getU32(in);
    std::string text(length, '\0');
    in.read(text.data(), static_cast<std::streamsize>(length));
    if (!in || in.gcount() != static_cast<std::streamsize>(length)) {
        throw std::runtime_error("block run file is truncated");
    }
    return text;
}

// One term's slice of a block run, as parsed by the merge cursors.
//
// A BlockTerm owns its postings and a flat position arena. During parsing,
// every Posting::posStart is rewritten to an offset into `positions` rather
// than keeping the absolute token position: the run is self-contained, so the
// merge phase can copy the slice without consulting the original index.
struct BlockTerm {
    std::string term;
    std::vector<Posting> postings;
    std::vector<std::uint32_t> positions;
};

// Sequential reader over a block run: hands out BlockTerms in the sorted
// order phase 1 wrote them.
//
// Run file layout (all integers little-endian u32):
//   termCount
//   repeated termCount times:
//     stringLength, stringBytes
//     postingCount
//     repeated postingCount times:
//       docID, tf, then tf absolute positions
//
// The u32 count prefix lets the cursor detect truncation before allocating a
// huge vector; getString/getU32 already reject short reads.
class RunCursor {
public:
    explicit RunCursor(const std::string& path) : path_(path), file_(path, std::ios::binary) {
        if (!file_) {
            throw std::runtime_error("cannot reopen block run: " + path);
        }
        remaining_ = getU32(file_);
        advance();  // load the first term (if any)
    }

    bool done() const { return current_.term.empty() && remaining_ == 0; }
    const BlockTerm& current() const { return current_; }
    const std::string& path() const { return path_; }

    // Move to the next term of this run.
    //
    // `remaining_` is decremented before reading so a corrupt count cannot
    // make the loop run forever. Each posting's positions are appended to the
    // per-term arena in read order; posStart is therefore assigned from the
    // current arena size, not read from disk (the run stores tf positions).
    void advance() {
        current_ = BlockTerm{};
        if (remaining_ == 0) {
            return;
        }
        --remaining_;
        try {
            current_.term = getString(file_);
            const std::uint32_t postings = getU32(file_);
            current_.postings.resize(postings);
            for (Posting& posting : current_.postings) {
                posting.docID = getU32(file_);
                posting.tf = getU32(file_);
                posting.posStart =
                    static_cast<std::uint32_t>(current_.positions.size());
                for (std::uint32_t k = 0; k < posting.tf; ++k) {
                    current_.positions.push_back(getU32(file_));
                }
            }
        } catch (const std::runtime_error& error) {
            throw std::runtime_error("while reading " + path_ + ": " + error.what());
        }
    }

private:
    std::string path_;
    std::ifstream file_;
    std::uint32_t remaining_ = 0;  // terms left in this run
    BlockTerm current_;
};

// Write one block run: terms sorted, postings in v1 encoding.
//
// `sorted` borrows PostingList pointers from the live dictionary, so the
// caller must not mutate or clear that dictionary until this returns. We flush
// explicitly and test the stream afterwards: a full disk must fail the build
// rather than silently produce a short run that the merge would trust.
std::uint64_t writeRun(const std::string& path,
                       const std::vector<std::pair<std::string, const PostingList*>>& sorted) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) {
        throw std::runtime_error("cannot write block run: " + path);
    }
    putU32(out, static_cast<std::uint32_t>(sorted.size()));
    for (const auto& [term, list] : sorted) {
        putString(out, term);
        putU32(out, static_cast<std::uint32_t>(list->postings().size()));
        for (const Posting& posting : list->postings()) {
            putU32(out, posting.docID);
            putU32(out, posting.tf);
            for (std::uint32_t k = 0; k < posting.tf; ++k) {
                putU32(out, list->positions()[posting.posStart + k]);
            }
        }
    }
    out.flush();
    if (!out) {
        throw std::runtime_error("error while writing block run: " + path);
    }
    return std::filesystem::file_size(path);
}

}  // namespace

// SPIMI-style external build.
//
// Why this exists: the in-memory builder keeps one dictionary entry per
// distinct term, so its peak memory grows with the vocabulary, not with the
// block budget. At 400M distinct terms that is tens of GB. This builder bounds
// the dictionary to roughly `blockTerms` entries: whenever the live map
// crosses the budget, it is sorted and spilled to a run file, then cleared.
//
// Invariants that make the merge a simple concatenation:
//   * A spill happens only between documents. Therefore no docID appears in
//     two runs for the same term, and docIDs in later runs are strictly larger
//     than docIDs in earlier runs.
//   * Within a run, terms are unique and sorted (std::sort over the map keys).
//   * Within a term, postings are appended in document order, so docIDs are
//     strictly increasing and positions within one posting are increasing.
// The merge below relies on all three: equal terms are concatenated in run
// order, and posStart is rebuilt rather than reused.
//
// Complexity: phase 1 tokenizes every document once and performs one sort per
// spill, O(totalTokens + R * B log B) where B is the block budget and R the
// number of runs. Phase 2 performs a linear scan of the live cursors for each
// output term: O(T * R) term comparisons and O(totalPostings) copying, with
// O(R) cursor memory. R grows with vocabulary/B, which is why a larger budget
// trades memory for fewer merge passes.
Index buildIndexBlocked(const Corpus& corpus, const BlockBuildOptions& options,
                        BlockBuildStats& stats) {
    if (options.blockTerms == 0) {
        throw std::runtime_error("blockTerms must be at least 1");
    }
    std::filesystem::create_directories(options.blockDir);
    const auto started = std::chrono::steady_clock::now();

    // ------------------------------------------------------------------
    // phase 1: stream documents, spill sorted runs at document boundaries
    // ------------------------------------------------------------------
    const Tokenizer tokenizer(options.build.tokenizer);
    std::vector<Token> tokens;
    // Stemming is pure but comparatively expensive, and the same surface form
    // recurs constantly ("the", "lord", ...). Caching surface -> stem keeps
    // phase 1 proportional to distinct forms rather than total tokens; the
    // cache is released when this function returns.
    std::unordered_map<std::string, std::string> stemCache;  // see count.cpp
    const auto stemmed = [&stemCache](const std::string& word) -> const std::string& {
        auto it = stemCache.find(word);
        if (it == stemCache.end()) {
            it = stemCache.emplace(word, stem(word)).first;
        }
        return it->second;
    };

    Index index;  // docs + tokenCount are corpus properties, blocks don't change them
    index.flags = (options.build.stopWords != nullptr ? kIndexFlagStopWords : 0u) |
                  (options.build.stem ? kIndexFlagStemmed : 0u);

    // The bounded dictionary. `live.size()` is the memory knob: each entry
    // owns a PostingList whose vectors grow with occurrences in this block.
    std::unordered_map<std::string, PostingList> live;  // the bounded dictionary
    std::vector<std::string> runPaths;
    const auto spill = [&]() {
        if (live.empty()) {
            return;
        }
        // Copy pointers into a sortable vector; the map itself stays untouched
        // until writeRun has consumed every pointer.
        std::vector<std::pair<std::string, const PostingList*>> sorted;
        sorted.reserve(live.size());
        for (const auto& entry : live) {
            sorted.emplace_back(entry.first, &entry.second);
        }
        std::sort(sorted.begin(), sorted.end(),
                  [](const auto& a, const auto& b) { return a.first < b.first; });
        const std::string path = options.blockDir + "/" + options.blockPrefix +
                                 std::to_string(runPaths.size()) + ".bin";
        stats.blockBytes += writeRun(path, sorted);
        runPaths.push_back(path);
        // clear() keeps the bucket array; that is fine because the next block
        // reuses the same budget and avoids rehashing from scratch.
        live.clear();
    };

    for (std::size_t i = 0; i < corpus.size(); ++i) {
        const Document& doc = corpus[i];
        tokens.clear();
        tokenizer.tokenize(doc, tokens);
        // Position counts only tokens that survive the stop-word filter, so a
        // phrase match sees the same numbering as the final index. Filtered
        // tokens are skipped before `position` is incremented.
        std::uint32_t position = 0;  // offset in the document's filtered stream
        for (const Token& token : tokens) {
            if (options.build.stopWords != nullptr &&
                options.build.stopWords->contains(token.text)) {
                continue;
            }
            const std::string& term = options.build.stem ? stemmed(token.text) : token.text;
            live[term].append(static_cast<std::uint32_t>(doc.id), position);
            ++position;
        }
        // IndexDoc::tokenCount records the filtered length. Phrase matching
        // and future length-normalized scoring use it, so it must agree with
        // the position counter above rather than with tokens.size().
        index.docs.push_back(IndexDoc{static_cast<std::uint32_t>(doc.id), doc.title, position});
        index.tokenCount += position;
        // Spill only at document boundaries: a term split across blocks must
        // never carry the same docID twice (docIDs rise with document order).
        // The check is >= because one document can push the dictionary past
        // the budget by its own distinct terms; that overshoot is bounded by
        // the largest document, not by the corpus vocabulary.
        if (live.size() >= options.blockTerms) {
            stats.maxLiveTerms = std::max(stats.maxLiveTerms, live.size());
            spill();
        }
    }
    spill();  // the final partial block
    stats.blocks = runPaths.size();
    if (stats.blocks == 0) {
        return index;  // empty corpus
    }
    stats.scanSeconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
    const auto mergeStarted = std::chrono::steady_clock::now();

    // ------------------------------------------------------------------
    // phase 2: k-way merge over the run streams (terms ascend per run, so
    // repeatedly taking the minimum current term yields global order)
    //
    // The merge is O(T * R) where T is the number of distinct terms and R the
    // number of runs: for each output term we scan all cursors once. A heap
    // would reduce this to O(T log R), but with R in the hundreds and terms
    // compared as short strings, the constant-factor difference is small and
    // the linear scan keeps the code and the failure modes simple.
    // ------------------------------------------------------------------
    std::vector<std::unique_ptr<RunCursor>> cursors;
    cursors.reserve(runPaths.size());
    for (const std::string& path : runPaths) {
        cursors.push_back(std::make_unique<RunCursor>(path));
    }
    // Track non-empty cursors so the outer loop stops as soon as every run is
    // exhausted, without rescanning all cursors forever on an empty tail.
    std::size_t alive = 0;
    for (const auto& cursor : cursors) {
        if (!cursor->done()) {
            ++alive;
        }
    }

    while (alive > 0) {
        // Find the smallest term across the live cursors. Ties are expected:
        // that is exactly how a term spread over several runs is detected.
        std::size_t minAt = cursors.size();
        for (std::size_t c = 0; c < cursors.size(); ++c) {
            if (cursors[c]->done()) {
                continue;
            }
            if (minAt == cursors.size() ||
                cursors[c]->current().term < cursors[minAt]->current().term) {
                minAt = c;
            }
        }
        // Concatenate this term's postings from every run holding it, in
        // run order == document order: docIDs stay strictly increasing and
        // each posting's posStart is rebuilt against the merged arena.
        // We must copy the Posting: its posStart refers to the run-local
        // position arena, not to `merged.positions`.
        IndexTerm merged;
        merged.term = cursors[minAt]->current().term;
        for (std::size_t c = 0; c < cursors.size(); ++c) {
            if (cursors[c]->done() ||
                cursors[c]->current().term != merged.term) {
                continue;
            }
            const BlockTerm& block = cursors[c]->current();
            for (const Posting& posting : block.postings) {
                Posting copy = posting;
                copy.posStart = static_cast<std::uint32_t>(merged.positions.size());
                merged.positions.insert(merged.positions.end(),
                                        block.positions.begin() + posting.posStart,
                                        block.positions.begin() + posting.posStart + posting.tf);
                merged.postings.push_back(copy);
            }
            cursors[c]->advance();
            if (cursors[c]->done()) {
                --alive;
            }
        }
        index.terms.push_back(std::move(merged));
    }
    stats.mergeSeconds =
        std::chrono::duration<double>(std::chrono::steady_clock::now() - mergeStarted).count();

    // Runs are an implementation detail; remove them once the cursors (and
    // their open streams) are gone -- Windows refuses to delete open files.
    // Cleanup failures are deliberately ignored: a leftover temp file is
    // harmless, while throwing here would discard a fully built index.
    cursors.clear();
    for (const std::string& path : runPaths) {
        std::filesystem::remove(path);
    }
    return index;
}

}  // namespace mse
