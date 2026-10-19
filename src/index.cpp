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
// tokenCount (see plan.md 7 for the full layout).
constexpr std::size_t kHeaderBytes = 4 + 4 + 4 + 4 + 4 + 8;

// Dump: how many positions of a single posting are printed before "...".
constexpr std::uint32_t kDumpPositionsPerPosting = 12;

void writeU32(std::ostream& out, std::uint32_t value) {
    const char bytes[4] = {static_cast<char>(value & 0xFFu),
                           static_cast<char>((value >> 8) & 0xFFu),
                           static_cast<char>((value >> 16) & 0xFFu),
                           static_cast<char>((value >> 24) & 0xFFu)};
    out.write(bytes, sizeof(bytes));
}

void writeU64(std::ostream& out, std::uint64_t value) {
    writeU32(out, static_cast<std::uint32_t>(value & 0xFFFFFFFFu));
    writeU32(out, static_cast<std::uint32_t>(value >> 32));
}

void writeString(std::ostream& out, const std::string& text) {
    writeU32(out, static_cast<std::uint32_t>(text.size()));
    out.write(text.data(), static_cast<std::streamsize>(text.size()));
}

std::uint64_t postingsBytesOf(const IndexTerm& term) {
    return static_cast<std::uint64_t>(term.postings.size()) * 8 +
           static_cast<std::uint64_t>(term.positions.size()) * 4;
}

// Byte offset of each term's postings block inside the file (the layout
// writeIndex() writes and the term table records). Shared by the file
// writer and the dictionary sidecar so both always agree.
std::vector<std::uint64_t> computeTermOffsets(const Index& index) {
    std::uint64_t offset = kHeaderBytes;
    for (const IndexDoc& doc : index.docs) {
        offset += 12 + doc.title.size();  // docID + keptTokens + length + title
    }
    for (const IndexTerm& term : index.terms) {
        offset += 28 + term.term.size();  // len + term + df + tf + offset + bytes
    }
    std::vector<std::uint64_t> termOffsets(index.terms.size());
    for (std::size_t i = 0; i < index.terms.size(); ++i) {
        termOffsets[i] = offset;
        offset += postingsBytesOf(index.terms[i]);
    }
    return termOffsets;
}

// ---------------------------------------------------------------------------
// little-endian reader with bounds checks (the load side of the spec)
// ---------------------------------------------------------------------------

class Reader {
public:
    explicit Reader(const std::vector<std::uint8_t>& data) : data_(data) {}

    void expectMagic() {
        require(4);
        if (std::memcmp(data_.data() + pos_, "MSE1", 4) != 0) {
            throw std::runtime_error("not an index file (bad magic)");
        }
        pos_ += 4;
    }

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

    std::uint64_t u64() {
        const std::uint64_t low = u32();
        const std::uint64_t high = u32();
        return low | (high << 32);
    }

    std::string string() {
        const std::uint32_t length = u32();
        require(length);
        std::string text(reinterpret_cast<const char*>(data_.data() + pos_), length);
        pos_ += length;
        return text;
    }

    void seek(std::uint64_t offset) {
        if (offset > data_.size()) {
            throw std::runtime_error("index file is truncated or corrupt");
        }
        pos_ = static_cast<std::size_t>(offset);
    }

    // Bytes left in the buffer (a postings block parses until it hits 0).
    std::size_t remaining() const { return data_.size() - pos_; }

    // One varint (see index.hpp for the byte-aligned code spec).
    std::uint32_t varint() {
        require(1);
        const std::uint8_t first = data_[pos_];
        std::size_t count;  // total bytes, from the first byte's prefix
        switch (first & 0xC0u) {
            case 0x40u: count = 1; break;  // 01xxxxxx
            case 0x80u: count = 2; break;  // 10xxxxxx
            case 0xC0u: count = 3; break;  // 11xxxxxx
            default:    count = 4; break;  // 00xxxxxx + 3 continuation bytes
        }
        require(count);
        std::uint32_t value = first & 0x3Fu;
        for (std::size_t i = 1; i < count; ++i) {
            value |= static_cast<std::uint32_t>(data_[pos_ + i]) << (6 + 8 * (i - 1));
        }
        pos_ += count;
        return value;
    }

private:
    void require(std::size_t bytes) {
        if (pos_ + bytes > data_.size()) {
            throw std::runtime_error("index file is truncated or corrupt");
        }
    }

    const std::vector<std::uint8_t>& data_;
    std::size_t pos_ = 0;
};

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

// ---------------------------------------------------------------------------
// varint + compressed postings blocks (plan.md 6.4)
// ---------------------------------------------------------------------------

// One term's postings as a v2 block: docID gap, tf, then position deltas.
// Decoding is symmetric (parse until the block's bytes are consumed).
std::string encodePostingsBlock(const IndexTerm& term) {
    std::string out;
    std::uint32_t previousDoc = 0;
    for (const Posting& posting : term.postings) {
        out += encodeVarint(posting.docID - previousDoc);  // >= 1: docIDs rise
        previousDoc = posting.docID;
        out += encodeVarint(posting.tf);
        std::uint32_t previousPos = 0;
        for (std::uint32_t k = 0; k < posting.tf; ++k) {
            const std::uint32_t position = term.positions[posting.posStart + k];
            out += encodeVarint(position - previousPos);
            previousPos = position;
        }
    }
    return out;
}

}  // namespace

std::string encodeVarint(std::uint32_t value) {
    if (value >= (1u << 30)) {
        throw std::runtime_error("varint value exceeds 30 bits");
    }
    std::string out;
    if (value < (1u << 6)) {
        out.push_back(static_cast<char>(0x40u | value));
    } else if (value < (1u << 14)) {
        out.push_back(static_cast<char>(0x80u | (value & 0x3Fu)));
        out.push_back(static_cast<char>((value >> 6) & 0xFFu));
    } else if (value < (1u << 22)) {
        out.push_back(static_cast<char>(0xC0u | (value & 0x3Fu)));
        out.push_back(static_cast<char>((value >> 6) & 0xFFu));
        out.push_back(static_cast<char>((value >> 14) & 0xFFu));
    } else {
        out.push_back(static_cast<char>(value & 0x3Fu));
        out.push_back(static_cast<char>((value >> 6) & 0xFFu));
        out.push_back(static_cast<char>((value >> 14) & 0xFFu));
        out.push_back(static_cast<char>((value >> 22) & 0xFFu));
    }
    return out;
}

std::uint32_t decodeVarint(const std::uint8_t*& cursor, const std::uint8_t* end) {
    if (cursor >= end) {
        throw std::runtime_error("varint runs past the block end");
    }
    const std::uint8_t first = *cursor++;
    std::size_t count;
    switch (first & 0xC0u) {
        case 0x40u: count = 1; break;
        case 0x80u: count = 2; break;
        case 0xC0u: count = 3; break;
        default:    count = 4; break;
    }
    if (cursor + (count - 1) > end) {
        throw std::runtime_error("varint runs past the block end");
    }
    std::uint32_t value = first & 0x3Fu;
    for (std::size_t i = 1; i < count; ++i) {
        value |= static_cast<std::uint32_t>(*cursor++) << (6 + 8 * (i - 1));
    }
    return value;
}

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
    index.flags = (options.stopWords != nullptr ? kIndexFlagStopWords : 0u) |
                  (options.stem ? kIndexFlagStemmed : 0u);
    index.docs.reserve(corpus.size());

    for (std::size_t i = 0; i < corpus.size(); ++i) {
        const Document& doc = corpus[i];
        tokens.clear();
        tokenizer.tokenize(doc, tokens);

        std::uint32_t position = 0;  // offset in the document's filtered stream
        for (const Token& token : tokens) {
            if (options.stopWords != nullptr &&
                options.stopWords->contains(token.text)) {
                continue;  // stop words do not occupy positions
            }
            const std::string& term =
                options.stem ? stemmed(token.text) : token.text;
            dictionary.upsert(term).postings.append(
                static_cast<std::uint32_t>(doc.id), position);
            ++position;
        }

        index.docs.push_back(IndexDoc{static_cast<std::uint32_t>(doc.id),
                                      doc.title, position});
        index.tokenCount += position;
    }

    // Freeze: BST in-order (or collect+sort for the hash table) gives the
    // lexicographic term table that the file format and queries rely on.
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

void writeIndex(const std::string& path, const Index& index) {
    // Pre-compute every offset so the file is written sequentially and
    // deterministically (no pointer fix-ups, same bytes for the same input).
    const std::vector<std::uint64_t> termOffsets = computeTermOffsets(index);

    std::ofstream out(path, std::ios::binary);
    if (!out) {
        throw std::runtime_error("cannot write index file: " + path);
    }

    out.write("MSE1", 4);
    writeU32(out, kIndexVersion);
    writeU32(out, index.flags);
    writeU32(out, static_cast<std::uint32_t>(index.docs.size()));
    writeU32(out, static_cast<std::uint32_t>(index.terms.size()));
    writeU64(out, index.tokenCount);

    for (const IndexDoc& doc : index.docs) {
        writeU32(out, doc.docID);
        writeU32(out, doc.keptTokens);
        writeString(out, doc.title);
    }
    for (std::size_t i = 0; i < index.terms.size(); ++i) {
        const IndexTerm& term = index.terms[i];
        writeString(out, term.term);
        writeU32(out, static_cast<std::uint32_t>(term.postings.size()));
        writeU64(out, static_cast<std::uint64_t>(term.positions.size()));
        writeU64(out, termOffsets[i]);
        writeU32(out, static_cast<std::uint32_t>(postingsBytesOf(term)));
    }
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

void writeIndexCompressed(const std::string& path, const Index& index) {
    // Pre-encode every postings block so the term table can record the
    // compressed (offset, length) pairs before the blocks are flushed --
    // one sequential write, deterministic bytes, same as the v1 writer.
    std::vector<std::string> blocks;
    blocks.reserve(index.terms.size());
    std::uint64_t offset = kHeaderBytes;
    for (const IndexDoc& doc : index.docs) {
        offset += 12 + doc.title.size();
    }
    for (const IndexTerm& term : index.terms) {
        offset += 28 + term.term.size();  // the term table row itself
    }
    std::vector<std::uint64_t> termOffsets(index.terms.size());
    for (std::size_t i = 0; i < index.terms.size(); ++i) {
        termOffsets[i] = offset;
        blocks.push_back(encodePostingsBlock(index.terms[i]));
        offset += blocks.back().size();
        if (blocks.back().size() >= (1u << kDictLenBits)) {
            throw std::runtime_error("term '" + index.terms[i].term +
                                     "' has a compressed block over 2 MiB");
        }
    }

    std::ofstream out(path, std::ios::binary);
    if (!out) {
        throw std::runtime_error("cannot write index file: " + path);
    }
    out.write("MSE1", 4);
    writeU32(out, kIndexVersionCompressed);
    writeU32(out, index.flags);
    writeU32(out, static_cast<std::uint32_t>(index.docs.size()));
    writeU32(out, static_cast<std::uint32_t>(index.terms.size()));
    writeU64(out, index.tokenCount);
    for (const IndexDoc& doc : index.docs) {
        writeU32(out, doc.docID);
        writeU32(out, doc.keptTokens);
        writeString(out, doc.title);
    }
    for (std::size_t i = 0; i < index.terms.size(); ++i) {
        const IndexTerm& term = index.terms[i];
        writeString(out, term.term);
        writeU32(out, static_cast<std::uint32_t>(term.postings.size()));
        writeU64(out, static_cast<std::uint64_t>(term.positions.size()));
        writeU64(out, termOffsets[i]);
        writeU32(out, static_cast<std::uint32_t>(blocks[i].size()));
    }
    for (const std::string& block : blocks) {
        out.write(block.data(), static_cast<std::streamsize>(block.size()));
    }
    if (!out) {
        throw std::runtime_error("error while writing index file: " + path);
    }
}

Index loadIndex(const std::string& path) {
    // Keep the buffer alive for the whole parse: Reader holds a reference to
    // it (binding the temporary directly to Reader would dangle immediately).
    const std::vector<std::uint8_t> data = readWholeFile(path);
    Reader reader(data);
    reader.expectMagic();
    const std::uint32_t version = reader.u32();
    if (version != kIndexVersion && version != kIndexVersionCompressed) {
        throw std::runtime_error("unsupported index version " +
                                 std::to_string(version));
    }

    Index index;
    index.flags = reader.u32();
    const std::uint32_t docCount = reader.u32();
    const std::uint32_t termCount = reader.u32();
    index.tokenCount = reader.u64();

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

    index.terms.reserve(termCount);
    for (RawTerm& source : raw) {
        reader.seek(source.offset);
        IndexTerm term;
        term.term = std::move(source.term);
        term.postings.reserve(source.df);
        std::uint64_t tfTotal = 0;
        // v1 blocks store raw u32s; v2 stores docID gaps / position deltas
        // in the byte-aligned varint (only the postings block encoding
        // differs between the versions -- plan.md 7).
        std::uint32_t previousDoc = 0;
        for (std::uint32_t d = 0; d < source.df; ++d) {
            Posting posting;
            if (version == kIndexVersionCompressed) {
                posting.docID = previousDoc + reader.varint();
                previousDoc = posting.docID;
                posting.tf = reader.varint();
            } else {
                posting.docID = reader.u32();
                posting.tf = reader.u32();
            }
            posting.posStart = static_cast<std::uint32_t>(term.positions.size());
            std::uint32_t previousPos = 0;
            for (std::uint32_t k = 0; k < posting.tf; ++k) {
                const std::uint32_t position =
                    version == kIndexVersionCompressed
                        ? previousPos + reader.varint()
                        : reader.u32();
                previousPos = position;
                term.positions.push_back(position);
            }
            tfTotal += posting.tf;
            term.postings.push_back(posting);
        }
        if (tfTotal != source.tf) {
            throw std::runtime_error("index file corrupt: tf mismatch for term '" +
                                     term.term + "'");
        }
        index.terms.push_back(std::move(term));
    }

    validateIndex(index);
    return index;
}

void validateIndex(const Index& index) {
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
    if (termTokens != index.tokenCount) {
        throw std::runtime_error("index invariant: term tf sum != tokenCount");
    }
}

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
            out << " ... (+" << (posting.tf - printed) << " more)";
        }
        out << "\n";
    }
    if (entry->postings.size() > shown) {
        out << "  ... (+" << (entry->postings.size() - shown) << " more postings)\n";
    }
    return true;
}

// ---------------------------------------------------------------------------
// disk dictionary sidecar (B-series)
// ---------------------------------------------------------------------------

void writeDictFile(const Index& index, const std::string& path,
                   std::uint32_t pageSize, bool compressed) {
    // index.terms is sorted by construction; the packed value keeps the
    // posting byte range, so the reader can seek without the term table.
    // Offsets follow whichever postings encoding the index file uses.
    std::vector<std::uint64_t> offsets;
    std::vector<std::uint64_t> lengths;
    if (compressed) {
        offsets.reserve(index.terms.size());
        lengths.reserve(index.terms.size());
        std::uint64_t offset = kHeaderBytes;
        for (const IndexDoc& doc : index.docs) {
            offset += 12 + doc.title.size();
        }
        for (const IndexTerm& term : index.terms) {
            offset += 28 + term.term.size();  // the term table rows
        }
        for (const IndexTerm& term : index.terms) {
            offsets.push_back(offset);
            const std::uint64_t len = encodePostingsBlock(term).size();
            lengths.push_back(len);
            offset += len;
        }
    } else {
        offsets = computeTermOffsets(index);
        lengths.reserve(index.terms.size());
        for (const IndexTerm& term : index.terms) {
            lengths.push_back(postingsBytesOf(term));
        }
    }
    std::vector<std::pair<std::string, std::uint64_t>> pairs;
    pairs.reserve(index.terms.size());
    for (std::size_t i = 0; i < index.terms.size(); ++i) {
        if (lengths[i] >= (1ull << kDictLenBits)) {
            throw std::runtime_error("term '" + index.terms[i].term +
                                     "' postings exceed the 2 MiB pack limit");
        }
        pairs.emplace_back(index.terms[i].term,
                           (offsets[i] << kDictLenBits) | lengths[i]);
    }
    BTree::bulkLoad(pairs, pageSize).writeFile(path);
}

LazyIndexReader::LazyIndexReader(const std::string& indexPath,
                                 const std::string& dictPath,
                                 std::size_t cachePages)
    : indexPath_(indexPath), dict_(dictPath, cachePages) {
    // Parse only the header and the doc table. The buffer grows in chunks
    // until the doc table is complete, so a lazy open reads just a few KiB
    // regardless of how large the term table + postings are (the point of
    // the sidecar: those regions are only touched per query term).
    file_.open(indexPath_, std::ios::binary);
    if (!file_) {
        throw std::runtime_error("cannot open index file: " + indexPath_);
    }
    constexpr std::size_t kChunk = 8192;
    std::vector<std::uint8_t> buffer;
    char chunk[kChunk];
    const auto grow = [&]() {
        file_.read(chunk, static_cast<std::streamsize>(kChunk));
        const std::streamsize got = file_.gcount();
        bytesRead_ += static_cast<std::uint64_t>(got);
        buffer.insert(buffer.end(), chunk, chunk + got);
        return got > 0;
    };
    grow();

    // First pass: header. termCount is cross-checked against the sidecar
    // so a mismatched pair cannot silently serve wrong postings.
    std::uint32_t docCount = 0;
    {
        Reader headerReader(buffer);
        headerReader.expectMagic();
        const std::uint32_t fileVersion = headerReader.u32();
        if (fileVersion != kIndexVersion && fileVersion != kIndexVersionCompressed) {
            throw std::runtime_error("unsupported index version in " + indexPath_);
        }
        version_ = fileVersion;
        flags_ = headerReader.u32();
        docCount = headerReader.u32();
        const std::uint32_t termCount = headerReader.u32();
        if (termCount != dict_.keyCount()) {
            throw std::runtime_error("dictionary/index mismatch: " +
                                     std::to_string(termCount) + " terms in " +
                                     indexPath_ + " vs " +
                                     std::to_string(dict_.keyCount()) +
                                     " in the dictionary");
        }
        tokenCount_ = headerReader.u64();
    }

    // Second pass: doc rows, re-parsing (cheaply, the table is small) as
    // the buffer grows past rows that ran out of bytes.
    while (docs_.size() < docCount) {
        try {
            Reader row(buffer);
            row.seek(kHeaderBytes);
            docs_.clear();
            for (std::uint32_t i = 0; i < docCount; ++i) {
                IndexDoc doc;
                doc.docID = row.u32();
                doc.keptTokens = row.u32();
                doc.title = row.string();
                docs_.push_back(std::move(doc));
            }
        } catch (const std::runtime_error&) {
            if (!grow()) {  // out of file before the table completed
                throw std::runtime_error("index file is truncated or corrupt: " +
                                         indexPath_);
            }
        }
    }
    // Rewind to the start: readTerm() seeks per term anyway.
    file_.clear();
    file_.seekg(0);
}

bool LazyIndexReader::readTerm(const std::string& term, IndexTerm& out) const {
    const auto packed = dict_.findValue(term);
    if (!packed.has_value()) {
        return false;
    }

    // Read exactly this term's posting block and parse it in place. The
    // stream is opened once and reused (mutable: readTerm is const).
    const std::uint64_t offset = *packed >> kDictLenBits;
    const std::uint64_t length = *packed & ((1ull << kDictLenBits) - 1);
    std::vector<std::uint8_t> block(static_cast<std::size_t>(length));
    file_.clear();
    file_.seekg(static_cast<std::streamoff>(offset));
    file_.read(reinterpret_cast<char*>(block.data()),
               static_cast<std::streamsize>(length));
    if (!file_ || file_.gcount() != static_cast<std::streamsize>(length)) {
        throw std::runtime_error("index file is truncated or corrupt: " +
                                 indexPath_);
    }
    bytesRead_ += length;
    Reader reader(block);
    out.term = term;
    out.postings.clear();
    out.positions.clear();
    std::uint32_t previousDoc = 0;  // gap decoding state, reset per block
    while (reader.remaining() > 0) {
        Posting posting;
        // v1 blocks store raw u32s; v2 blocks store gaps/deltas as varints
        // (the version came from the header in the constructor).
        if (version_ == kIndexVersionCompressed) {
            posting.docID = previousDoc + reader.varint();
            previousDoc = posting.docID;
            posting.tf = reader.varint();
        } else {
            posting.docID = reader.u32();
            posting.tf = reader.u32();
        }
        posting.posStart =
            static_cast<std::uint32_t>(out.positions.size());
        std::uint32_t previousPos = 0;  // position deltas, per posting
        for (std::uint32_t k = 0; k < posting.tf; ++k) {
            const std::uint32_t position =
                version_ == kIndexVersionCompressed
                    ? previousPos + reader.varint()
                    : reader.u32();
            previousPos = position;
            out.positions.push_back(position);
        }
        out.postings.push_back(posting);
    }
    return true;
}

std::vector<std::pair<std::string, std::uint64_t>> LazyIndexReader::allTerms()
    const {
    return dict_.rangeScan("", "\xff\xff\xff\xff");
}

}  // namespace mse
