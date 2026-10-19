// index.cpp -- index build, v1 serialization, loading, validation, dump.

#include "index.hpp"

#include <algorithm>
#include <cstring>
#include <fstream>
#include <iterator>
#include <ostream>
#include <stdexcept>
#include <unordered_map>

#include "stemmer.hpp"
#include "stopwords.hpp"

namespace mse {

namespace {

// Fixed part of the header: magic + version + flags + docCount + termCount +
// tokenCount (see plan.md 7 for the full layout). Keeping this as a named
// constant makes the offset pre-computation in writeIndex() self-documenting.
//   4 ("MSE1") + 4 version + 4 flags + 4 docs + 4 terms + 8 tokens = 28.
constexpr std::size_t kHeaderBytes = 4 + 4 + 4 + 4 + 4 + 8;

// Dump: how many positions of a single posting are printed before "…".
// Posting lists can be long (the, and, ...), and a full dump is unreadable;
// the count is only a display cap, never a storage limit.
constexpr std::uint32_t kDumpPositionsPerPosting = 12;

// Write one 32-bit little-endian value.
//
// The explicit byte shifts (rather than a memcpy of the host representation)
// make the file format independent of host endianness and of struct padding.
void writeU32(std::ostream& out, std::uint32_t value) {
    const char bytes[4] = {static_cast<char>(value & 0xFFu),
                           static_cast<char>((value >> 8) & 0xFFu),
                           static_cast<char>((value >> 16) & 0xFFu),
                           static_cast<char>((value >> 24) & 0xFFu)};
    out.write(bytes, sizeof(bytes));
}

// Write one 64-bit little-endian value as two 32-bit halves (low first).
// Reusing writeU32 guarantees the same byte order on every platform.
void writeU64(std::ostream& out, std::uint64_t value) {
    writeU32(out, static_cast<std::uint32_t>(value & 0xFFFFFFFFu));
    writeU32(out, static_cast<std::uint32_t>(value >> 32));
}

// Write a length-prefixed string: u32 byte length followed by exactly that
// many bytes. No terminator and no encoding conversion -- terms are ASCII
// lowercase stems, and a length prefix makes reading O(1) and unambiguous.
void writeString(std::ostream& out, const std::string& text) {
    writeU32(out, static_cast<std::uint32_t>(text.size()));
    out.write(text.data(), static_cast<std::streamsize>(text.size()));
}

// Size in bytes of one term's postings block in the file:
//   df * (docID u32 + tf u32) + total positions * u32.
// The loader uses this only for the offset table; the actual read iterates df
// records and follows their tf counts.
std::uint64_t postingsBytesOf(const IndexTerm& term) {
    return static_cast<std::uint64_t>(term.postings.size()) * 8 +
           static_cast<std::uint64_t>(term.positions.size()) * 4;
}

// ---------------------------------------------------------------------------
// little-endian reader with bounds checks (the load side of the spec)
// ---------------------------------------------------------------------------

// A cursor over an in-memory index file.
//
// Every read checks that enough bytes remain, so a truncated or corrupt file
// produces a clear exception instead of an out-of-bounds read. The reader is
// deliberately tiny and mirrors the writer byte for byte; keeping both sides
// in one file makes format drift easy to spot. It stores a reference to the
// caller's buffer, so that buffer must outlive the Reader (see loadIndex).
class Reader {
public:
    explicit Reader(const std::vector<std::uint8_t>& data) : data_(data) {}

    // Verify the "MSE1" magic and advance past it. A wrong magic usually
    // means the user passed an unrelated file (e.g. a text dump).
    void expectMagic() {
        require(4);
        if (std::memcmp(data_.data() + pos_, "MSE1", 4) != 0) {
            throw std::runtime_error("not an index file (bad magic)");
        }
        pos_ += 4;
    }

    // Read one u32 by assembling the four bytes explicitly (little-endian).
    std::uint32_t u32() {
        require(4);
        const std::uint32_t value =
            static_cast<std::uint32_t>(data_[pos_]) |
            (static_cast<std::uint32_t>(data_[pos_ + 1]) << 8) |
            (static_cast<std::uint32_t>(data_[pos_ + 2]) << 16) |
            (static_cast<std::uint32_t>(data_[pos_ + 3]) << 24);
        pos_ += 4;
        return value;
    }

    // Read one u64 as two little-endian u32 halves (low half first).
    std::uint64_t u64() {
        const std::uint64_t low = u32();
        const std::uint64_t high = u32();
        return low | (high << 32);
    }

    // Read a u32 length followed by that many bytes.
    std::string string() {
        const std::uint32_t length = u32();
        require(length);
        std::string text(reinterpret_cast<const char*>(data_.data() + pos_), length);
        pos_ += length;
        return text;
    }

    // Jump to an absolute byte offset. The offset must be inside the buffer
    // (equal to the size is allowed: an empty read would then fail its own
    // bounds check, which is also correct).
    void seek(std::uint64_t offset) {
        if (offset > data_.size()) {
            throw std::runtime_error("index file is truncated or corrupt");
        }
        pos_ = static_cast<std::size_t>(offset);
    }

private:
    // Throw unless `bytes` more bytes are available at the cursor. Every
    // primitive calls this before touching the buffer.
    void require(std::size_t bytes) {
        if (pos_ + bytes > data_.size()) {
            throw std::runtime_error("index file is truncated or corrupt");
        }
    }

    const std::vector<std::uint8_t>& data_;
    std::size_t pos_ = 0;
};

// Read an entire index file into memory.
//
// The v1 format is random-access (term table followed by offset-addressed
// posting blocks), so the simplest correct loader slurps the file and lets
// Reader seek inside it. Binary mode is required: the bytes are not text.
// Throws with the path on failure. Complexity: O(file size).
std::vector<std::uint8_t> readWholeFile(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        throw std::runtime_error("cannot open index file: " + path);
    }
    std::vector<std::uint8_t> data((std::istreambuf_iterator<char>(in)),
                                   std::istreambuf_iterator<char>());
    if (in.bad()) {
        throw std::runtime_error("error while reading index file: " + path);
    }
    return data;
}

}  // namespace

// Build the full in-memory index over `corpus`.
//
// The three normalization stages are applied in the same order as the query
// side (tokenize -> stop words -> stem), and their flags are recorded in the
// header so a later query can reproduce the pipeline. The dictionary is
// supplied by the caller (splay or hash); entries are copied into the frozen
// Index, so the dictionary remains usable for statistics and comparison.
//
// Position semantics: `position` counts tokens *after* stop word removal,
// starting at 0 per document. Stop words therefore leave no gaps, which is
// what makes phrase adjacency on the filtered stream well-defined.
//
// Complexity: O(total tokens) plus dictionary costs; memory O(#terms +
// #postings + #positions).
Index buildIndex(const Corpus& corpus, const BuildOptions& options,
                 Dictionary& dictionary) {
    const Tokenizer tokenizer(options.tokenizer);
    std::vector<Token> tokens;
    // Stemming is deterministic: each distinct form only needs one lookup.
    std::unordered_map<std::string, std::string> stemCache;
    const auto stemmed = [&stemCache](const std::string& word) -> const std::string& {
        auto it = stemCache.find(word);
        if (it == stemCache.end()) {
            it = stemCache.emplace(word, stem(word)).first;
        }
        return it->second;
    };

    Index index;
    // Record exactly which optional stages ran. The query side reads these
    // bits instead of being told again on the command line, preventing a
    // "built with stemming, queried without" mismatch.
    index.flags = (options.stopWords != nullptr ? kIndexFlagStopWords : 0u) |
                  (options.stem ? kIndexFlagStemmed : 0u);
    index.docs.reserve(corpus.size());

    for (std::size_t i = 0; i < corpus.size(); ++i) {
        const Document& doc = corpus[i];
        tokens.clear();
        tokenizer.tokenize(doc, tokens);

        // Offset in this document's *filtered* stream. Stop words are skipped
        // before the increment, so they consume no position; phrase queries
        // therefore see a contiguous token sequence.
        std::uint32_t position = 0;
        for (const Token& token : tokens) {
            if (options.stopWords != nullptr &&
                options.stopWords->contains(token.text)) {
                continue;  // stop words do not occupy positions
            }
            const std::string& term =
                options.stem ? stemmed(token.text) : token.text;
            // One upsert per occurrence: the dictionary returns the term's
            // entry, and the posting list updates the last document's tf or
            // opens a new posting. Documents are visited in increasing docID
            // order, which PostingList::append relies on.
            dictionary.upsert(term).postings.append(
                static_cast<std::uint32_t>(doc.id), position);
            ++position;
        }

        // keptTokens is the document's contribution to tokenCount and is
        // validated against the term-side sums when the index is loaded.
        index.docs.push_back(IndexDoc{static_cast<std::uint32_t>(doc.id),
                                      doc.title, position});
        index.tokenCount += position;
    }

    // Freeze: BST in-order (or collect+sort for the hash table) gives the
    // lexicographic term table that the file format and queries rely on.
    // Copying the entries into the Index decouples it from the dictionary:
    // the dictionary may be destroyed (or reused for a second build) without
    // affecting the index, and the Index owns exactly the data it serializes.
    const std::vector<TermEntry*> sorted = dictionary.sortedEntries();
    index.terms.reserve(sorted.size());
    for (const TermEntry* entry : sorted) {
        IndexTerm term;
        term.term = entry->term;
        term.postings = entry->postings.postings();
        term.positions = entry->postings.positions();
        index.terms.push_back(std::move(term));
    }
    return index;
}

// Serialize `index` to the v1 binary format (little-endian).
//
// Layout: fixed header, document table, term table, then all postings blocks.
// The term table stores each term's absolute postings offset and byte size, so
// the loader can jump directly to a term without scanning earlier postings.
//
// Offsets are pre-computed before any byte is written. The file is then
// produced strictly sequentially, which (a) avoids seeking and (b) guarantees
// that the same Index always yields the same bytes -- the property the
// splay-vs-hash and round-trip tests rely on. Throws on I/O failure.
// Complexity: O(#docs + #terms + #postings + #positions).
void writeIndex(const std::string& path, const Index& index) {
    // Pre-compute every offset so the file is written sequentially and
    // deterministically (no pointer fix-ups, same bytes for the same input).
    //
    // First skip the header and the document table. Each doc row is
    // docID(4) + keptTokens(4) + title length(4) + title bytes.
    std::uint64_t offset = kHeaderBytes;
    for (const IndexDoc& doc : index.docs) {
        offset += 12 + doc.title.size();  // docID + keptTokens + length + title
    }
    // Then skip the term table. Each row is term length(4) + term bytes +
    // df(4) + tf(8) + offset(8) + bytes(4) = 28 + term size.
    for (const IndexTerm& term : index.terms) {
        offset += 28 + term.term.size();  // len + term + df + tf + offset + bytes
    }
    // What remains is the postings section. Record where each term's block
    // starts, then advance by that block's exact byte size.
    std::vector<std::uint64_t> termOffsets(index.terms.size());
    for (std::size_t i = 0; i < index.terms.size(); ++i) {
        termOffsets[i] = offset;
        offset += postingsBytesOf(index.terms[i]);
    }

    std::ofstream out(path, std::ios::binary);
    if (!out) {
        throw std::runtime_error("cannot write index file: " + path);
    }

    // Header: fixed fields in the order Reader expects.
    out.write("MSE1", 4);
    writeU32(out, kIndexVersion);
    writeU32(out, index.flags);
    writeU32(out, static_cast<std::uint32_t>(index.docs.size()));
    writeU32(out, static_cast<std::uint32_t>(index.terms.size()));
    writeU64(out, index.tokenCount);

    // Document table: docID, kept-token count and title (length-prefixed).
    // Titles are not used for ranking, but they let dump/query print without
    // reloading the corpus.
    for (const IndexDoc& doc : index.docs) {
        writeU32(out, doc.docID);
        writeU32(out, doc.keptTokens);
        writeString(out, doc.title);
    }
    // Term table, already in lexicographic order. df/tf are derived from the
    // posting data but stored redundantly so queries can report statistics
    // and validate consistency without scanning the postings first.
    for (std::size_t i = 0; i < index.terms.size(); ++i) {
        const IndexTerm& term = index.terms[i];
        writeString(out, term.term);
        writeU32(out, static_cast<std::uint32_t>(term.postings.size()));
        writeU64(out, static_cast<std::uint64_t>(term.positions.size()));
        writeU64(out, termOffsets[i]);
        writeU32(out, static_cast<std::uint32_t>(postingsBytesOf(term)));
    }
    // Postings section: for each term, df records of {docID, tf, positions}.
    // position slices are contiguous because PostingList::append stores them
    // that way; posStart is implicit in the read order and not written.
    for (const IndexTerm& term : index.terms) {
        for (const Posting& posting : term.postings) {
            writeU32(out, posting.docID);
            writeU32(out, posting.tf);
            for (std::uint32_t k = 0; k < posting.tf; ++k) {
                writeU32(out, term.positions[posting.posStart + k]);
            }
        }
    }
    if (!out) {
        throw std::runtime_error("error while writing index file: " + path);
    }
}

// Load and validate an index file produced by writeIndex().
//
// The parser follows the writer's layout exactly: header, document table,
// term table (with offsets), then each term's posting block via seek. After
// parsing, validateIndex() checks every structural invariant; a corrupt file
// therefore fails loudly instead of producing wrong query results.
//
// Throws std::runtime_error on I/O errors, bad magic/version, truncation, or
// invariant violations. Complexity: O(file size).
Index loadIndex(const std::string& path) {
    // Keep the buffer alive for the whole parse: Reader holds a reference to
    // it (binding the temporary directly to Reader would dangle immediately).
    // This exact mistake was caught by the golden-sample test (notes.md 4.2 #6).
    const std::vector<std::uint8_t> data = readWholeFile(path);
    Reader reader(data);
    reader.expectMagic();
    const std::uint32_t version = reader.u32();
    if (version != kIndexVersion) {
        throw std::runtime_error("unsupported index version " +
                                 std::to_string(version));
    }

    // Counts and flags come from the file; they drive both allocation and the
    // query pipeline. The writer guarantees the layout that follows.
    Index index;
    index.flags = reader.u32();
    const std::uint32_t docCount = reader.u32();
    const std::uint32_t termCount = reader.u32();
    index.tokenCount = reader.u64();

    // Document table. docID order is validated later by validateIndex().
    index.docs.reserve(docCount);
    for (std::uint32_t i = 0; i < docCount; ++i) {
        IndexDoc doc;
        doc.docID = reader.u32();
        doc.keptTokens = reader.u32();
        doc.title = reader.string();
        index.docs.push_back(std::move(doc));
    }

    // Read the term table first, then jump to each term's postings block via
    // its recorded offset (this also exercises the offset fields).
    //
    // The raw rows are buffered because the postings blocks live after the
    // whole term table: after moving the cursor to one block we could not
    // continue reading the next table row without remembering our place.
    // Buffering makes the sequential term-table read independent of seeking.
    struct RawTerm {
        std::string term;
        std::uint32_t df = 0;
        std::uint64_t tf = 0;
        std::uint64_t offset = 0;
        std::uint32_t bytes = 0;
    };
    std::vector<RawTerm> raw;
    raw.reserve(termCount);
    for (std::uint32_t i = 0; i < termCount; ++i) {
        RawTerm term;
        term.term = reader.string();
        term.df = reader.u32();
        term.tf = reader.u64();
        term.offset = reader.u64();
        term.bytes = reader.u32();
        raw.push_back(std::move(term));
    }

    // Rebuild each term. posStart is not stored in the file: positions for a
    // term's documents are written back-to-back, so the current arena size is
    // exactly the start of the next slice. This is why the loader must append
    // positions in file order.
    index.terms.reserve(termCount);
    for (RawTerm& source : raw) {
        reader.seek(source.offset);
        IndexTerm term;
        term.term = std::move(source.term);
        term.postings.reserve(source.df);
        std::uint64_t tfTotal = 0;
        for (std::uint32_t d = 0; d < source.df; ++d) {
            Posting posting;
            posting.docID = reader.u32();
            posting.tf = reader.u32();
            posting.posStart = static_cast<std::uint32_t>(term.positions.size());
            for (std::uint32_t k = 0; k < posting.tf; ++k) {
                term.positions.push_back(reader.u32());
            }
            tfTotal += posting.tf;
            term.postings.push_back(posting);
        }
        // df/tf are stored redundantly; a mismatch means the file is corrupt
        // (or the writer/reader formats have drifted).
        if (tfTotal != source.tf) {
            throw std::runtime_error("index file corrupt: tf mismatch for term '" +
                                     term.term + "'");
        }
        index.terms.push_back(std::move(term));
    }

    validateIndex(index);
    return index;
}

// Check the structural invariants that every consumer relies on.
//
// These are not just defensive checks: they encode the contract between the
// builder, the serializer and the query engine. If one fails, the index is
// corrupt, and throwing is safer than returning partial/incorrect results.
// The checks are linear in the index size and run after every load; build
// also calls them before writing.
//
// Invariants:
//   * docIDs are exactly 1..N;
//   * sum(doc.keptTokens) == tokenCount;
//   * terms are non-empty and strictly increasing;
//   * every posting list is non-empty;
//   * within a term, docIDs are strictly increasing and in range;
//   * tf > 0, posStart is dense (no gaps or overlap), and
//     sum(tf) == positions.size();
//   * sum over terms of positions == tokenCount (both sides agree).
void validateIndex(const Index& index) {
    // Document side: IDs must be 1..N, and per-document kept-token counts must
    // add up to the global token count.
    std::uint64_t docTokens = 0;
    for (std::size_t i = 0; i < index.docs.size(); ++i) {
        if (index.docs[i].docID != i + 1) {
            throw std::runtime_error("index invariant: docIDs are not dense");
        }
        docTokens += index.docs[i].keptTokens;
    }
    if (docTokens != index.tokenCount) {
        throw std::runtime_error("index invariant: doc token sum != tokenCount");
    }

    // Term side: sorted, non-empty, consistent postings. The strict "<"
    // comparison also rejects duplicate terms, which would make binary search
    // ambiguous.
    std::uint64_t termTokens = 0;
    for (std::size_t i = 0; i < index.terms.size(); ++i) {
        const IndexTerm& term = index.terms[i];
        if (term.term.empty()) {
            throw std::runtime_error("index invariant: empty term");
        }
        if (i > 0 && !(index.terms[i - 1].term < term.term)) {
            throw std::runtime_error("index invariant: term table not strictly sorted");
        }
        if (term.postings.empty()) {
            throw std::runtime_error("index invariant: empty posting list for '" +
                                     term.term + "'");
        }
        // Positions of one term are stored in one arena. Each posting's
        // posStart must equal the number of positions seen so far, i.e. the
        // slices are dense, non-overlapping and in docID order.
        std::uint64_t positionsSeen = 0;
        std::uint32_t lastDoc = 0;
        for (const Posting& posting : term.postings) {
            if (posting.docID <= lastDoc || posting.docID > index.docs.size()) {
                throw std::runtime_error("index invariant: bad docID in '" +
                                         term.term + "'");
            }
            if (posting.tf == 0) {
                throw std::runtime_error("index invariant: zero tf in '" +
                                         term.term + "'");
            }
            if (posting.posStart != positionsSeen) {
                throw std::runtime_error("index invariant: positions are not dense in '" +
                                         term.term + "'");
            }
            lastDoc = posting.docID;
            positionsSeen += posting.tf;
        }
        if (positionsSeen != term.positions.size()) {
            throw std::runtime_error("index invariant: tf != positions in '" +
                                     term.term + "'");
        }
        termTokens += positionsSeen;
    }
    // Both views of the token count must agree: sum over documents (above)
    // and sum over terms (here). A mismatch means some token was attributed
    // to the wrong term or document.
    if (termTokens != index.tokenCount) {
        throw std::runtime_error("index invariant: term tf sum != tokenCount");
    }
}

// Binary-search the frozen term table for `term`.
//
// The table is strictly sorted (validated on load), so lower_bound is exact.
// Returns nullptr when the term is absent; the returned pointer is owned by
// `index` and remains valid as long as the Index lives. Complexity:
// O(log #terms) comparisons.
const IndexTerm* findIndexTerm(const Index& index, const std::string& term) {
    const auto it = std::lower_bound(
        index.terms.begin(), index.terms.end(), term,
        [](const IndexTerm& candidate, const std::string& value) {
            return candidate.term < value;
        });
    if (it == index.terms.end() || it->term != term) {
        return nullptr;
    }
    return &*it;
}

// Print one term's posting list in a human-readable table.
//
// Postings and positions are capped for readability: `maxPostings` limits the
// number of documents shown, and each posting prints at most
// kDumpPositionsPerPosting positions with a "...(+N more)" suffix. The dump is
// a debugging/inspection tool, not a data export (the CSV/index files are the
// machine-readable outputs). Returns false when the term is absent.
bool dumpTerm(const Index& index, const std::string& term, std::size_t maxPostings,
              std::ostream& out) {
    const IndexTerm* entry = findIndexTerm(index, term);
    if (entry == nullptr) {
        return false;
    }

    out << "term " << entry->term << ": df=" << entry->postings.size()
        << ", tf=" << entry->positions.size() << "\n";
    const std::size_t shown = std::min(maxPostings, entry->postings.size());
    for (std::size_t i = 0; i < shown; ++i) {
        const Posting& posting = entry->postings[i];
        const std::string& title = index.docs[posting.docID - 1].title;
        out << "  doc " << posting.docID << " \"" << title << "\"  tf="
            << posting.tf << "  positions:";
        const std::uint32_t printed =
            std::min(posting.tf, kDumpPositionsPerPosting);
        for (std::uint32_t k = 0; k < printed; ++k) {
            out << ' ' << entry->positions[posting.posStart + k];
        }
        if (posting.tf > printed) {
            out << " …(+" << (posting.tf - printed) << " more)";
        }
        out << "\n";
    }
    if (entry->postings.size() > shown) {
        out << "  …(+" << (entry->postings.size() - shown) << " more postings)\n";
    }
    return true;
}

}  // namespace mse
