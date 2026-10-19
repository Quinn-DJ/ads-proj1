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
struct BlockTerm {
    std::string term;
    std::vector<Posting> postings;
    std::vector<std::uint32_t> positions;
};

// Sequential reader over a block run: hands out BlockTerms in the sorted
// order phase 1 wrote them.
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

    std::unordered_map<std::string, PostingList> live;  // the bounded dictionary
    std::vector<std::string> runPaths;
    const auto spill = [&]() {
        if (live.empty()) {
            return;
        }
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
        live.clear();
    };

    for (std::size_t i = 0; i < corpus.size(); ++i) {
        const Document& doc = corpus[i];
        tokens.clear();
        tokenizer.tokenize(doc, tokens);
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
        index.docs.push_back(IndexDoc{static_cast<std::uint32_t>(doc.id), doc.title, position});
        index.tokenCount += position;
        // Spill only at document boundaries: a term split across blocks must
        // never carry the same docID twice (docIDs rise with document order).
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
    // ------------------------------------------------------------------
    std::vector<std::unique_ptr<RunCursor>> cursors;
    cursors.reserve(runPaths.size());
    for (const std::string& path : runPaths) {
        cursors.push_back(std::make_unique<RunCursor>(path));
    }
    std::size_t alive = 0;
    for (const auto& cursor : cursors) {
        if (!cursor->done()) {
            ++alive;
        }
    }

    while (alive > 0) {
        // Find the smallest term across the live cursors.
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
    cursors.clear();
    for (const std::string& path : runPaths) {
        std::filesystem::remove(path);
    }
    return index;
}

}  // namespace mse
